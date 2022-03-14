// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Contains the implementation of class Tpm

#include "cryptohome/tpm2_impl.h"

#include <cinttypes>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/check.h>
#include <base/logging.h>
#include <base/message_loop/message_pump_type.h>
#include <base/notreached.h>
#include <base/numerics/safe_conversions.h>
#include <base/strings/stringprintf.h>
#include <crypto/libcrypto-compat.h>
#include <crypto/scoped_openssl_types.h>
#include <libhwsec/error/tpm_retry_handler.h>
#include <libhwsec/error/tpm2_error.h>
#include <libhwsec-foundation/crypto/aes.h>
#include <libhwsec-foundation/crypto/big_num_util.h>
#include <libhwsec-foundation/crypto/elliptic_curve.h>
#include <libhwsec-foundation/crypto/rsa.h>
#include <libhwsec-foundation/crypto/sha.h>
#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>
#include <tpm_manager-client/tpm_manager/dbus-constants.h>
#include <trunks/authorization_delegate.h>
#include <trunks/blob_parser.h>
#include <trunks/error_codes.h>
#include <trunks/openssl_utility.h>
#include <trunks/policy_session.h>
#include <trunks/tpm_alerts.h>
#include <trunks/tpm_constants.h>
#include <trunks/trunks_dbus_proxy.h>
#include <trunks/trunks_factory.h>
#include <trunks/trunks_factory_impl.h>

#include "cryptohome/crypto/elliptic_curve_error.h"

using brillo::Blob;
using brillo::BlobFromString;
using brillo::BlobToString;
using brillo::SecureBlob;
using hwsec::StatusChain;
using hwsec::TPM2Error;
using hwsec::TPMError;
using hwsec::TPMErrorBase;
using hwsec::TPMRetryAction;
using hwsec_foundation::CreateBigNumContext;
using hwsec_foundation::EllipticCurve;
using hwsec_foundation::ObscureRsaMessage;
using hwsec_foundation::PasskeyToAesKey;
using hwsec_foundation::ScopedBN_CTX;
using hwsec_foundation::SecureBlobToBigNum;
using hwsec_foundation::Sha256;
using hwsec_foundation::UnobscureRsaMessage;
using hwsec_foundation::error::CreateError;
using hwsec_foundation::error::WrapError;
using trunks::GetErrorString;
using trunks::TPM_RC_SUCCESS;
using trunks::TrunksFactory;

namespace cryptohome {

namespace {

constexpr trunks::TPMI_ECC_CURVE kDefaultTpmCurveId = trunks::TPM_ECC_NIST_P256;

constexpr EllipticCurve::CurveType kDefaultCurve =
    EllipticCurve::CurveType::kPrime256;

constexpr int kMinPassBlobSize = 32;

// Returns the total number of bits set in the first |size| elements from
// |array|.
int CountSetBits(const uint8_t* array, size_t size) {
  int res = 0;
  for (size_t i = 0; i < size; ++i) {
    for (int bit_position = 0; bit_position < 8; ++bit_position) {
      if ((array[i] & (1 << bit_position)) != 0) {
        ++res;
      }
    }
  }
  return res;
}

std::string OwnerDependencyEnumClassToString(
    Tpm::TpmOwnerDependency dependency) {
  switch (dependency) {
    case Tpm::TpmOwnerDependency::kInstallAttributes:
      return tpm_manager::kTpmOwnerDependency_Nvram;
    case Tpm::TpmOwnerDependency::kAttestation:
      return tpm_manager::kTpmOwnerDependency_Attestation;
    default:
      NOTREACHED() << __func__ << ": Unexpected enum class value: "
                   << static_cast<int>(dependency);
      return "";
  }
}

trunks::TpmUtility::AsymmetricKeyUsage ConvertAsymmetricKeyUsage(
    AsymmetricKeyUsage usage) {
  switch (usage) {
    case AsymmetricKeyUsage::kDecryptKey:
      return trunks::TpmUtility::AsymmetricKeyUsage::kDecryptKey;
    case AsymmetricKeyUsage::kSignKey:
      return trunks::TpmUtility::AsymmetricKeyUsage::kSignKey;
    case AsymmetricKeyUsage::kDecryptAndSignKey:
      return trunks::TpmUtility::AsymmetricKeyUsage::kDecryptAndSignKey;
    default:
      NOTREACHED() << __func__ << ": Unexpected enum class value: "
                   << static_cast<int>(usage);
      return trunks::TpmUtility::AsymmetricKeyUsage::kDecryptKey;
  }
}

StatusChain<TPMErrorBase> DeriveTpmEccPointFromSeed(
    const SecureBlob& seed, trunks::TPMS_ECC_POINT* out_point) {
  // Generate an ECC private key (scalar) based on the seed.
  crypto::ScopedBIGNUM private_key = SecureBlobToBigNum(Sha256(seed));

  ScopedBN_CTX context = CreateBigNumContext();
  if (!context.get()) {
    return CreateError<TPMError>("Failed to allocate BN_CTX structure",
                                 TPMRetryAction::kNoRetry);
  }

  std::optional<EllipticCurve> ec =
      EllipticCurve::Create(kDefaultCurve, context.get());
  if (!ec) {
    return CreateError<TPMError>("Failed to create EllipticCurve",
                                 TPMRetryAction::kNoRetry);
  }

  if (!ec->IsScalarValid(*private_key)) {
    // Generate another pass_blob may resolve this issue.
    return CreateError<EllipticCurveError>(
        EllipticCurveErrorCode::kScalarOutOfRange);
  }

  crypto::ScopedEC_POINT public_point =
      ec->MultiplyWithGenerator(*private_key, context.get());

  if (!public_point) {
    return CreateError<TPMError>("Failed to multiply with generator",
                                 TPMRetryAction::kNoRetry);
  }

  if (!trunks::OpensslToTpmEccPoint(*ec->GetGroup(), *public_point,
                                    ec->AffineCoordinateSizeInBytes(),
                                    out_point)) {
    return CreateError<TPMError>("Error converting OpenSSL to TPM ECC point",
                                 TPMRetryAction::kNoRetry);
  }

  return nullptr;
}

std::map<uint32_t, std::string> ToStrPcrMap(
    const std::map<uint32_t, brillo::Blob>& pcr_map) {
  std::map<uint32_t, std::string> str_pcr_map;
  for (const auto& [index, value] : pcr_map) {
    str_pcr_map[index] = brillo::BlobToString(value);
  }
  return str_pcr_map;
}
}  // namespace

// Keep it with sync to UMA enum list
// https://chromium.googlesource.com/chromium/src/+/HEAD/tools/metrics/histograms/enums.xml
// These values are persisted to logs, and should therefore never be renumbered
// nor reused.
enum TpmAlerts {
  kCamoBreach = 1,
  kDmemParity = 2,
  kDrfParity = 3,
  kImemParity = 4,
  kPgmFault = 5,
  kCpuDIfBusError = 6,
  kCpuDIfUpdateWatchdog = 7,
  kCpuIIfBusError = 8,
  kCpuIIfUpdateWatchdog = 9,
  kCpuSIfBusError = 10,
  kCpuSIfUpdateWatchdog = 11,
  kDmaIfBusErr = 12,
  kDmaIfUpdateWatchdog = 13,
  kSpsIfBusErr = 14,
  kSpsIfUpdateWatchdog = 15,
  kUsbIfBusErr = 16,
  kUsbIfUpdateWatchdog = 17,
  kFuseDefaults = 18,
  kDiffFail = 19,
  kSoftwareAlert0 = 20,
  kSoftwareAlert1 = 21,
  kSoftwareAlert2 = 22,
  kSoftwareAlert3 = 23,
  kHearbitFail = 24,
  kProcOpcodeHash = 25,
  kSramParityScrub = 26,
  kAesExecCtrMax = 27,
  kAesHkey = 28,
  kCertLookup = 29,
  kFlashEntry = 30,
  kPw = 31,
  kShaExecCtrMax = 32,
  kShaFault = 33,
  kShaHkey = 34,
  kPmuBatteryMon = 35,
  kPmuWatchdog = 36,
  kRtcDead = 37,
  kTempMax = 38,
  kTempMaxDiff = 39,
  kTempMin = 40,
  kRngOutOfSpec = 41,
  kRngTimeout = 42,
  kVoltageError = 43,
  kXoJitteryTrim = 44,

  kTPMAlertNumBuckets,  // Must be the last entry.
};
static_assert(kTPMAlertNumBuckets <= trunks::kAlertsMaxSize + 1,
              "Number of UMA enums less than alerts set size");

// Maps alerts identifiers received from TMP firmware to UMA identifiers
const TpmAlerts h1AlertsMap[trunks::kH1AlertsSize] = {
    kCamoBreach,
    kDmemParity,
    kDrfParity,
    kImemParity,
    kPgmFault,
    kCpuDIfBusError,
    kCpuDIfUpdateWatchdog,
    kCpuIIfBusError,
    kCpuIIfUpdateWatchdog,
    kCpuSIfBusError,
    kCpuSIfUpdateWatchdog,
    kDmaIfBusErr,
    kDmaIfUpdateWatchdog,
    kSpsIfBusErr,
    kSpsIfUpdateWatchdog,
    kUsbIfBusErr,
    kUsbIfUpdateWatchdog,
    kFuseDefaults,
    kDiffFail,
    kSoftwareAlert0,
    kSoftwareAlert1,
    kSoftwareAlert2,
    kSoftwareAlert3,
    kHearbitFail,
    kProcOpcodeHash,
    kSramParityScrub,
    kAesExecCtrMax,
    kAesHkey,
    kCertLookup,
    kFlashEntry,
    kPw,
    kShaExecCtrMax,
    kShaFault,
    kShaHkey,
    kPmuBatteryMon,
    kPmuWatchdog,
    kRtcDead,
    kTempMax,
    kTempMaxDiff,
    kTempMin,
    kRngOutOfSpec,
    kRngTimeout,
    kVoltageError,
    kXoJitteryTrim,
};

Tpm2Impl::Tpm2Impl(TrunksFactory* factory,
                   tpm_manager::TpmManagerUtility* tpm_manager_utility)
    : tpm_manager_utility_(tpm_manager_utility),
      has_external_trunks_context_(true) {
  external_trunks_context_.factory = factory;
  external_trunks_context_.tpm_state = factory->GetTpmState();
  external_trunks_context_.tpm_utility = factory->GetTpmUtility();
}

bool Tpm2Impl::InitializeTpmManagerUtility() {
  if (!tpm_manager_utility_) {
    tpm_manager_utility_ = tpm_manager::TpmManagerUtility::GetSingleton();
    if (!tpm_manager_utility_) {
      LOG(ERROR) << __func__ << ": Failed to get TpmManagerUtility singleton!";
    }
  }
  return tpm_manager_utility_ && tpm_manager_utility_->Initialize();
}

bool Tpm2Impl::CacheTpmManagerStatus() {
  if (!InitializeTpmManagerUtility()) {
    LOG(ERROR) << __func__ << ": Failed to initialize |TpmManagerUtility|.";
    return false;
  }
  return tpm_manager_utility_->GetTpmStatus(&is_enabled_, &is_owned_,
                                            &last_tpm_manager_data_);
}

bool Tpm2Impl::IsEnabled() {
  if (!is_enabled_) {
    if (!CacheTpmManagerStatus()) {
      LOG(ERROR) << __func__ << ": Failed to call |UpdateTpmStatus|.";
      return false;
    }
  }
  return is_enabled_;
}

bool Tpm2Impl::IsOwned() {
  if (!is_owned_) {
    if (!UpdateTpmStatus(RefreshType::REFRESH_IF_NEEDED)) {
      LOG(ERROR) << __func__ << ": Failed to call |UpdateTpmStatus|.";
      return false;
    }
  }
  return is_owned_;
}

bool Tpm2Impl::IsOwnerPasswordPresent() {
  if (!InitializeTpmManagerUtility()) {
    LOG(ERROR) << __func__ << ": failed to initialize |TpmManagerUtility|.";
    return false;
  }
  bool is_owner_password_present = false;
  if (!tpm_manager_utility_->GetTpmNonsensitiveStatus(
          nullptr, nullptr, &is_owner_password_present, nullptr)) {
    LOG(ERROR) << __func__ << ": Failed to get |is_owner_password_present|.";
    return false;
  }
  return is_owner_password_present;
}

bool Tpm2Impl::HasResetLockPermissions() {
  if (!InitializeTpmManagerUtility()) {
    LOG(ERROR) << __func__ << ": failed to initialize |TpmManagerUtility|.";
    return false;
  }
  bool has_reset_lock_permissions = false;
  if (!tpm_manager_utility_->GetTpmNonsensitiveStatus(
          nullptr, nullptr, nullptr, &has_reset_lock_permissions)) {
    LOG(ERROR) << __func__ << ": Failed to get |has_reset_lock_permissions|.";
    return false;
  }
  return has_reset_lock_permissions;
}

StatusChain<hwsec::TPMErrorBase> Tpm2Impl::GetRandomDataBlob(
    size_t length, brillo::Blob* data) {
  brillo::SecureBlob blob(length);
  if (StatusChain<TPMErrorBase> err = GetRandomDataSecureBlob(length, &blob)) {
    return WrapError<TPMError>(std::move(err), "GetRandomDataBlob failed");
  }
  data->assign(blob.begin(), blob.end());
  return nullptr;
}

StatusChain<hwsec::TPMErrorBase> Tpm2Impl::GetRandomDataSecureBlob(
    size_t length, brillo::SecureBlob* data) {
  CHECK(data);
  TrunksClientContext* trunks;
  if (!GetTrunksContext(&trunks)) {
    return CreateError<TPMError>("Failed to get trunks context",
                                 TPMRetryAction::kNoRetry);
  }
  std::string random_data;
  if (StatusChain<TPMErrorBase> err = HANDLE_TPM_COMM_ERROR(
          CreateError<TPM2Error>(trunks->tpm_utility->GenerateRandom(
              length, /* delegate */ nullptr, &random_data)))) {
    return WrapError<TPMError>(std::move(err), "Error getting random data");
  }
  if (random_data.size() != length) {
    return CreateError<TPMError>(
        base::StringPrintf("Error getting random data: requested length %zu"
                           ", received length %zu",
                           length, random_data.size()),
        TPMRetryAction::kNoRetry);
  }
  data->assign(random_data.begin(), random_data.end());
  return nullptr;
}

StatusChain<hwsec::TPMErrorBase> Tpm2Impl::GetAlertsData(
    Tpm::AlertsData* alerts) {
  TrunksClientContext* trunks;
  if (!GetTrunksContext(&trunks)) {
    return CreateError<TPMError>("Failed to get trunks context",
                                 TPMRetryAction::kNoRetry);
  }

  trunks::TpmAlertsData trunks_alerts;
  if (StatusChain<TPMErrorBase> err =
          HANDLE_TPM_COMM_ERROR(CreateError<TPM2Error>(
              trunks->tpm_utility->GetAlertsData(&trunks_alerts)))) {
    if (err.Is<TPM2Error>() &&
        err.Cast<TPM2Error>()->ErrorCode() == trunks::TPM_RC_NO_SUCH_COMMAND) {
      return WrapError<TPMError>(
          std::move(err), "TPM GetAlertsData vendor command is not implemented",
          TPMRetryAction::kNoRetry);
    } else {
      memset(alerts, 0, sizeof(Tpm::AlertsData));
      return WrapError<TPMError>(std::move(err), "Error getting alerts data");
    }
  } else if (trunks_alerts.chip_family != trunks::kFamilyH1) {
    // Currently we support only H1 alerts
    return CreateError<TPMError>(
        "Unknown alerts family: " + std::to_string(trunks_alerts.chip_family),
        TPMRetryAction::kNoRetry);
  }

  memset(alerts, 0, sizeof(Tpm::AlertsData));
  for (int i = 0; i < trunks_alerts.alerts_num; i++) {
    size_t uma_idx = h1AlertsMap[i];
    if (uma_idx <= 0 || uma_idx >= kTPMAlertNumBuckets) {
      LOG(ERROR) << "Alert index " << i << " maps into invalid UMA enum index "
                 << uma_idx;
    } else {
      alerts->counters[uma_idx] = trunks_alerts.counters[i];
    }
  }

  return nullptr;
}

bool Tpm2Impl::DefineNvram(uint32_t index, size_t length, uint32_t flags) {
  if (!InitializeTpmManagerUtility()) {
    LOG(ERROR) << __func__ << ": Failed to initialize |TpmManagerUtility|.";
    return false;
  }
  const bool write_define = flags & Tpm::kTpmNvramWriteDefine;
  const bool bind_to_pcr0 = flags & Tpm::kTpmNvramBindToPCR0;
  const bool firmware_readable = flags & Tpm::kTpmNvramFirmwareReadable;

  return tpm_manager_utility_->DefineSpace(index, length, write_define,
                                           bind_to_pcr0, firmware_readable);
}

bool Tpm2Impl::DestroyNvram(uint32_t index) {
  if (!InitializeTpmManagerUtility()) {
    LOG(ERROR) << __func__ << ": Failed to initialize |TpmManagerUtility|.";
    return false;
  }
  return tpm_manager_utility_->DestroySpace(index);
}

bool Tpm2Impl::WriteNvram(uint32_t index, const SecureBlob& blob) {
  if (!InitializeTpmManagerUtility()) {
    LOG(ERROR) << __func__ << ": Failed to initialize |TpmManagerUtility|.";
    return false;
  }
  return tpm_manager_utility_->WriteSpace(index, blob.to_string(),
                                          /*use_owner_auth=*/false);
}

bool Tpm2Impl::OwnerWriteNvram(uint32_t index, const SecureBlob& blob) {
  if (!InitializeTpmManagerUtility()) {
    LOG(ERROR) << __func__ << ": Failed to initialize |TpmManagerUtility|.";
    return false;
  }
  return tpm_manager_utility_->WriteSpace(index, blob.to_string(),
                                          /*use_owner_auth=*/true);
}

bool Tpm2Impl::ReadNvram(uint32_t index, SecureBlob* blob) {
  if (!InitializeTpmManagerUtility()) {
    return false;
  }

  std::string output;
  const bool result = tpm_manager_utility_->ReadSpace(index, false, &output);
  SecureBlob tmp(output);
  blob->swap(tmp);
  return result;
}

bool Tpm2Impl::IsNvramDefined(uint32_t index) {
  if (!InitializeTpmManagerUtility()) {
    LOG(ERROR) << __func__ << ": Failed to initialize |TpmManagerUtility|.";
    return false;
  }
  std::vector<uint32_t> spaces;
  if (!tpm_manager_utility_->ListSpaces(&spaces)) {
    return false;
  }
  for (uint32_t space : spaces) {
    if (index == space) {
      return true;
    }
  }
  return false;
}

bool Tpm2Impl::IsNvramLocked(uint32_t index) {
  if (!InitializeTpmManagerUtility()) {
    LOG(ERROR) << __func__ << ": Failed to initialize |TpmManagerUtility|.";
    return false;
  }
  uint32_t size;
  bool is_read_locked;
  bool is_write_locked;
  if (!tpm_manager_utility_->GetSpaceInfo(index, &size, &is_read_locked,
                                          &is_write_locked,
                                          /*attributes=*/nullptr)) {
    return false;
  }
  return is_write_locked;
}

bool Tpm2Impl::WriteLockNvram(uint32_t index) {
  if (!InitializeTpmManagerUtility()) {
    LOG(ERROR) << __func__ << ": Failed to initialize |TpmManagerUtility|.";
    return false;
  }
  return tpm_manager_utility_->LockSpace(index);
}

unsigned int Tpm2Impl::GetNvramSize(uint32_t index) {
  if (!InitializeTpmManagerUtility()) {
    LOG(ERROR) << __func__ << ": Failed to initialize |TpmManagerUtility|.";
    return false;
  }
  uint32_t size;
  bool is_read_locked;
  bool is_write_locked;
  if (!tpm_manager_utility_->GetSpaceInfo(index, &size, &is_read_locked,
                                          &is_write_locked,
                                          /*attributes=*/nullptr)) {
    return 0;
  }
  return size;
}

bool Tpm2Impl::CreateDelegate(const std::set<uint32_t>& bound_pcrs,
                              uint8_t delegate_family_label,
                              uint8_t delegate_label,
                              Blob* delegate_blob,
                              Blob* delegate_secret) {
  LOG(ERROR) << __func__ << ": Not implemented.";
  return false;
}

bool Tpm2Impl::Sign(const SecureBlob& key_blob,
                    const SecureBlob& input,
                    uint32_t bound_pcr_index,
                    SecureBlob* signature) {
  TrunksClientContext* trunks;
  if (!GetTrunksContext(&trunks)) {
    return false;
  }
  trunks::AuthorizationDelegate* delegate;
  std::unique_ptr<trunks::PolicySession> policy_session;
  std::unique_ptr<trunks::HmacSession> hmac_session;
  if (bound_pcr_index != kNotBoundToPCR) {
    policy_session = trunks->factory->GetPolicySession();
    if (StatusChain<TPMErrorBase> err =
            HANDLE_TPM_COMM_ERROR(CreateError<TPM2Error>(
                policy_session->StartUnboundSession(true, false)))) {
      LOG(ERROR) << "Error starting policy session: " << err;
      return false;
    }
    if (StatusChain<TPMErrorBase> err = HANDLE_TPM_COMM_ERROR(
            CreateError<TPM2Error>(policy_session->PolicyPCR(
                std::map<uint32_t, std::string>({{bound_pcr_index, ""}}))))) {
      LOG(ERROR) << "Error creating PCR policy: " << err;
      return false;
    }
    delegate = policy_session->GetDelegate();
  } else {
    hmac_session = trunks->factory->GetHmacSession();
    if (StatusChain<TPMErrorBase> err =
            HANDLE_TPM_COMM_ERROR(CreateError<TPM2Error>(
                hmac_session->StartUnboundSession(true, true)))) {
      LOG(ERROR) << "Error starting hmac session: " << err;
      return false;
    }
    hmac_session->SetEntityAuthorizationValue("");
    delegate = hmac_session->GetDelegate();
  }

  ScopedKeyHandle handle;
  if (StatusChain<TPMErrorBase> err = LoadWrappedKey(key_blob, &handle)) {
    LOG(ERROR) << "Error loading pcr bound key: " << err;
    return false;
  }
  std::string tpm_signature;
  if (StatusChain<TPMErrorBase> err = HANDLE_TPM_COMM_ERROR(
          CreateError<TPM2Error>(trunks->tpm_utility->Sign(
              handle.value(), trunks::TPM_ALG_RSASSA, trunks::TPM_ALG_SHA256,
              input.to_string(), true /* generate_hash */, delegate,
              &tpm_signature)))) {
    LOG(ERROR) << "Error signing: " << err;
    return false;
  }
  signature->assign(tpm_signature.begin(), tpm_signature.end());
  return true;
}

bool Tpm2Impl::CreatePCRBoundKey(
    const std::map<uint32_t, brillo::Blob>& pcr_map,
    AsymmetricKeyUsage key_type,
    SecureBlob* key_blob,
    SecureBlob* public_key_der,
    SecureBlob* creation_blob) {
  CHECK(key_blob) << "No key blob argument provided.";
  CHECK(creation_blob) << "No creation blob argument provided.";
  TrunksClientContext* trunks;
  if (!GetTrunksContext(&trunks)) {
    return false;
  }
  std::string policy_digest;
  std::map<uint32_t, std::string> str_pcr_map = ToStrPcrMap(pcr_map);
  if (StatusChain<TPMErrorBase> err =
          HANDLE_TPM_COMM_ERROR(CreateError<TPM2Error>(
              trunks->tpm_utility->GetPolicyDigestForPcrValues(
                  str_pcr_map, false /* use_auth_value */, &policy_digest)))) {
    LOG(ERROR) << "Error getting policy digest: " << err;
    return false;
  }
  std::vector<uint32_t> pcr_list;
  for (const auto& map_pair : pcr_map) {
    pcr_list.push_back(map_pair.first);
  }
  std::string tpm_key_blob;
  std::string tpm_creation_blob;
  std::unique_ptr<trunks::AuthorizationDelegate> delegate =
      trunks->factory->GetPasswordAuthorization("");
  if (StatusChain<TPMErrorBase> err = HANDLE_TPM_COMM_ERROR(
          CreateError<TPM2Error>(trunks->tpm_utility->CreateRSAKeyPair(
              ConvertAsymmetricKeyUsage(key_type), kDefaultTpmRsaModulusSize,
              kDefaultTpmPublicExponent,
              "",  // No authorization
              policy_digest,
              true,  // use_only_policy_authorization
              pcr_list, delegate.get(), &tpm_key_blob,
              &tpm_creation_blob /* No creation_blob */)))) {
    LOG(ERROR) << "Error creating a pcr bound key: " << err;
    return false;
  }
  key_blob->assign(tpm_key_blob.begin(), tpm_key_blob.end());
  creation_blob->assign(tpm_creation_blob.begin(), tpm_creation_blob.end());

  // if |public_key_der| is present, create and assign it.
  if (public_key_der) {
    trunks::TPM2B_PUBLIC public_data;
    trunks::TPM2B_PRIVATE private_data;
    if (!trunks->factory->GetBlobParser()->ParseKeyBlob(
            key_blob->to_string(), &public_data, &private_data)) {
      return false;
    }
    if (!PublicAreaToPublicKeyDER(public_data.public_area, public_key_der)) {
      return false;
    }
  }
  return true;
}

bool Tpm2Impl::VerifyPCRBoundKey(
    const std::map<uint32_t, brillo::Blob>& pcr_map,
    const SecureBlob& key_blob,
    const SecureBlob& creation_blob) {
  TrunksClientContext* trunks;
  if (!GetTrunksContext(&trunks)) {
    return false;
  }
  // First we verify that the PCR were in a known good state at the time of
  // Key creation.
  trunks::TPM2B_CREATION_DATA creation_data;
  trunks::TPM2B_DIGEST creation_hash;
  trunks::TPMT_TK_CREATION creation_ticket;
  if (!trunks->factory->GetBlobParser()->ParseCreationBlob(
          creation_blob.to_string(), &creation_data, &creation_hash,
          &creation_ticket)) {
    LOG(ERROR) << "Error parsing creation_blob.";
    return false;
  }
  trunks::TPML_PCR_SELECTION& pcr_select =
      creation_data.creation_data.pcr_select;
  if (pcr_select.count != 1) {
    LOG(ERROR) << "Creation data missing creation PCR value.";
    return false;
  }
  if (pcr_select.pcr_selections[0].hash != trunks::TPM_ALG_SHA256) {
    LOG(ERROR) << "Creation PCR extended with wrong hash algorithm.";
    return false;
  }
  uint8_t* pcr_selections = pcr_select.pcr_selections[0].pcr_select;
  if (pcr_map.size() != CountSetBits(pcr_selections, PCR_SELECT_MIN)) {
    LOG(ERROR) << "Incorrect creation PCR specified.";
    return false;
  }
  brillo::Blob concatenated_pcr_values;
  for (const auto& map_pair : pcr_map) {
    uint32_t pcr_index = map_pair.first;
    const brillo::Blob& pcr_value = map_pair.second;
    if (pcr_index >= 8 * PCR_SELECT_MIN ||
        (pcr_selections[pcr_index / 8] & (1 << (pcr_index % 8))) == 0) {
      LOG(ERROR) << "Incorrect creation PCR specified.";
      return false;
    }
    concatenated_pcr_values.insert(concatenated_pcr_values.end(),
                                   pcr_value.begin(), pcr_value.end());
  }
  Blob expected_pcr_digest = Sha256(concatenated_pcr_values);
  if (creation_data.creation_data.pcr_digest.size !=
      expected_pcr_digest.size()) {
    LOG(ERROR) << "Incorrect PCR digest size.";
    return false;
  }
  if (memcmp(creation_data.creation_data.pcr_digest.buffer,
             expected_pcr_digest.data(), expected_pcr_digest.size()) != 0) {
    LOG(ERROR) << "Incorrect PCR digest value.";
    return false;
  }
  // Then we certify that the key was created by the TPM.
  ScopedKeyHandle scoped_handle;
  if (StatusChain<TPMErrorBase> err =
          LoadWrappedKey(key_blob, &scoped_handle)) {
    LOG(ERROR) << "Failed to load wrapped key: " << err;
    return false;
  }
  if (StatusChain<TPMErrorBase> err = HANDLE_TPM_COMM_ERROR(
          CreateError<TPM2Error>(trunks->tpm_utility->CertifyCreation(
              scoped_handle.value(), creation_blob.to_string())))) {
    LOG(ERROR) << "Error certifying that key was created by TPM: " << err;
    return false;
  }
  // Finally we verify that the key's policy_digest is the expected value.
  std::unique_ptr<trunks::PolicySession> trial_session =
      trunks->factory->GetTrialSession();
  if (StatusChain<TPMErrorBase> err =
          HANDLE_TPM_COMM_ERROR(CreateError<TPM2Error>(
              trial_session->StartUnboundSession(true, true)))) {
    LOG(ERROR) << "Error starting a trial session: " << err;
    return false;
  }
  std::map<uint32_t, std::string> str_pcr_map = ToStrPcrMap(pcr_map);
  if (StatusChain<TPMErrorBase> err = HANDLE_TPM_COMM_ERROR(
          CreateError<TPM2Error>(trial_session->PolicyPCR(str_pcr_map)))) {
    LOG(ERROR) << "Error restricting trial policy to pcr value: " << err;
    return false;
  }
  std::string policy_digest;
  if (StatusChain<TPMErrorBase> err = HANDLE_TPM_COMM_ERROR(
          CreateError<TPM2Error>(trial_session->GetDigest(&policy_digest)))) {
    LOG(ERROR) << "Error getting policy digest: " << err;
    return false;
  }
  trunks::TPMT_PUBLIC public_area;
  if (StatusChain<TPMErrorBase> err = HANDLE_TPM_COMM_ERROR(
          CreateError<TPM2Error>(trunks->tpm_utility->GetKeyPublicArea(
              scoped_handle.value(), &public_area)))) {
    LOG(ERROR) << "Error getting key public area: " << err;
    return false;
  }
  if (public_area.auth_policy.size != policy_digest.size()) {
    LOG(ERROR) << "Key auth policy and policy digest are of different length."
               << public_area.auth_policy.size << "," << policy_digest.size();
    return false;
  } else if (memcmp(public_area.auth_policy.buffer, policy_digest.data(),
                    policy_digest.size()) != 0) {
    LOG(ERROR) << "Key auth policy is different from policy digest.";
    return false;
  } else if (public_area.object_attributes & trunks::kUserWithAuth) {
    LOG(ERROR) << "Key authorization is not restricted to policy.";
    return false;
  }
  return true;
}

bool Tpm2Impl::ExtendPCR(uint32_t pcr_index, const Blob& extension) {
  TrunksClientContext* trunks;
  if (!GetTrunksContext(&trunks)) {
    return false;
  }
  std::unique_ptr<trunks::AuthorizationDelegate> delegate =
      trunks->factory->GetPasswordAuthorization("");
  if (StatusChain<TPMErrorBase> err = HANDLE_TPM_COMM_ERROR(
          CreateError<TPM2Error>(trunks->tpm_utility->ExtendPCR(
              pcr_index, BlobToString(extension), delegate.get())))) {
    LOG(ERROR) << "Error extending PCR: " << err;
    return false;
  }
  if (StatusChain<TPMErrorBase> err = HANDLE_TPM_COMM_ERROR(
          CreateError<TPM2Error>(trunks->tpm_utility->ExtendPCRForCSME(
              pcr_index, BlobToString(extension))))) {
    LOG(ERROR) << "Error extending PCR for CSME: " << err;
    return false;
  }
  return true;
}

bool Tpm2Impl::ReadPCR(uint32_t pcr_index, Blob* pcr_value) {
  CHECK(pcr_value);
  TrunksClientContext* trunks;
  if (!GetTrunksContext(&trunks)) {
    return false;
  }
  std::string pcr_digest;
  if (StatusChain<TPMErrorBase> err =
          HANDLE_TPM_COMM_ERROR(CreateError<TPM2Error>(
              trunks->tpm_utility->ReadPCR(pcr_index, &pcr_digest)))) {
    LOG(ERROR) << "Error reading from PCR: " << err;
    return false;
  }
  *pcr_value = BlobFromString(pcr_digest);
  return true;
}

bool Tpm2Impl::WrapRsaKey(const SecureBlob& public_modulus,
                          const SecureBlob& prime_factor,
                          SecureBlob* wrapped_key) {
  TrunksClientContext* trunks;
  if (!GetTrunksContext(&trunks)) {
    return false;
  }
  std::string key_blob;
  std::unique_ptr<trunks::AuthorizationDelegate> delegate =
      trunks->factory->GetPasswordAuthorization("");
  if (StatusChain<TPMErrorBase> err = HANDLE_TPM_COMM_ERROR(
          CreateError<TPM2Error>(trunks->tpm_utility->ImportRSAKey(
              trunks::TpmUtility::AsymmetricKeyUsage::kDecryptKey,
              public_modulus.to_string(), kDefaultTpmPublicExponent,
              prime_factor.to_string(),
              "",  // No authorization,
              delegate.get(), &key_blob)))) {
    LOG(ERROR) << "Error creating SRK wrapped key: " << err;
    return false;
  }
  wrapped_key->assign(key_blob.begin(), key_blob.end());
  return true;
}

bool Tpm2Impl::CreateWrappedEccKey(SecureBlob* wrapped_key) {
  CHECK(wrapped_key) << "No key blob argument provided.";
  TrunksClientContext* trunks;
  if (!GetTrunksContext(&trunks)) {
    return false;
  }
  std::vector<uint32_t> pcr_list;
  std::string tpm_key_blob;
  std::string tpm_creation_blob;
  std::unique_ptr<trunks::AuthorizationDelegate> delegate =
      trunks->factory->GetPasswordAuthorization("");
  if (StatusChain<TPMErrorBase> err = HANDLE_TPM_COMM_ERROR(
          CreateError<TPM2Error>(trunks->tpm_utility->CreateECCKeyPair(
              trunks::TpmUtility::kDecryptKey, kDefaultTpmCurveId,
              "",     // No authorization
              "",     // No policy digest
              false,  // use_only_policy_authorization
              pcr_list, delegate.get(), &tpm_key_blob,
              &tpm_creation_blob /* No creation_blob */)))) {
    LOG(ERROR) << "Error creating a pcr bound key: " << err;
    return false;
  }
  wrapped_key->assign(tpm_key_blob.begin(), tpm_key_blob.end());

  return true;
}

StatusChain<TPMErrorBase> Tpm2Impl::LoadWrappedKey(
    const SecureBlob& wrapped_key, ScopedKeyHandle* key_handle) {
  CHECK(key_handle);
  TrunksClientContext* trunks;
  if (!GetTrunksContext(&trunks)) {
    return CreateError<TPMError>("Failed to get trunks context",
                                 TPMRetryAction::kNoRetry);
  }
  trunks::TPM_HANDLE handle;
  std::unique_ptr<trunks::AuthorizationDelegate> delegate =
      trunks->factory->GetPasswordAuthorization("");
  if (StatusChain<TPMErrorBase> err = HANDLE_TPM_COMM_ERROR(
          CreateError<TPM2Error>(trunks->tpm_utility->LoadKey(
              wrapped_key.to_string(), delegate.get(), &handle)))) {
    return WrapError<TPMError>(std::move(err), "Error loading SRK wrapped key");
  }
  key_handle->reset(this, handle);
  return nullptr;
}

bool Tpm2Impl::LegacyLoadCryptohomeKey(ScopedKeyHandle* key_handle,
                                       SecureBlob* key_blob) {
  // This doesn't apply to devices with TPM 2.0.
  return false;
}

void Tpm2Impl::CloseHandle(TpmKeyHandle key_handle) {
  TrunksClientContext* trunks;
  if (!GetTrunksContext(&trunks)) {
    return;
  }
  trunks->factory->GetTpm()->FlushContext(
      key_handle, nullptr,
      base::BindRepeating(
          [](TpmKeyHandle key_handle, trunks::TPM_RC result) {
            if (StatusChain<TPMErrorBase> err =
                    CreateError<TPM2Error>(result)) {
              LOG(WARNING) << "Error flushing tpm handle " << key_handle << ": "
                           << err;
            }
          },
          key_handle));
}

StatusChain<TPMErrorBase> Tpm2Impl::EncryptBlob(TpmKeyHandle key_handle,
                                                const SecureBlob& plaintext,
                                                const SecureBlob& key,
                                                SecureBlob* ciphertext) {
  CHECK(ciphertext);
  TrunksClientContext* trunks;
  if (!GetTrunksContext(&trunks)) {
    return CreateError<TPMError>("Failed to get trunks context",
                                 TPMRetryAction::kNoRetry);
  }
  std::string tpm_ciphertext;
  if (StatusChain<TPMErrorBase> err = HANDLE_TPM_COMM_ERROR(
          CreateError<TPM2Error>(trunks->tpm_utility->AsymmetricEncrypt(
              key_handle, trunks::TPM_ALG_OAEP, trunks::TPM_ALG_SHA256,
              plaintext.to_string(), nullptr, &tpm_ciphertext)))) {
    return WrapError<TPMError>(std::move(err), "Error encrypting plaintext");
  }
  if (!ObscureRsaMessage(SecureBlob(tpm_ciphertext), key, ciphertext)) {
    return CreateError<TPMError>("Error obscuring tpm encrypted blob",
                                 TPMRetryAction::kNoRetry);
  }
  return nullptr;
}

StatusChain<TPMErrorBase> Tpm2Impl::DecryptBlob(
    TpmKeyHandle key_handle,
    const SecureBlob& ciphertext,
    const SecureBlob& key,
    const std::map<uint32_t, brillo::Blob>& pcr_map,
    SecureBlob* plaintext) {
  CHECK(plaintext);
  TrunksClientContext* trunks;
  if (!GetTrunksContext(&trunks)) {
    return CreateError<TPMError>("Failed to get trunks context",
                                 TPMRetryAction::kNoRetry);
  }
  SecureBlob local_data;
  if (!UnobscureRsaMessage(ciphertext, key, &local_data)) {
    return CreateError<TPMError>("Error unobscureing message",
                                 TPMRetryAction::kNoRetry);
  }
  trunks::AuthorizationDelegate* delegate;
  std::unique_ptr<trunks::PolicySession> policy_session;
  std::unique_ptr<trunks::AuthorizationDelegate> default_delegate;
  if (!pcr_map.empty()) {
    std::map<uint32_t, std::string> str_pcr_map = ToStrPcrMap(pcr_map);
    policy_session = trunks->factory->GetPolicySession();
    if (StatusChain<TPMErrorBase> err =
            HANDLE_TPM_COMM_ERROR(CreateError<TPM2Error>(
                policy_session->StartUnboundSession(true, true)))) {
      return WrapError<TPMError>(std::move(err),
                                 "Error starting policy session",
                                 TPMRetryAction::kNoRetry);
    }
    if (StatusChain<TPMErrorBase> err = HANDLE_TPM_COMM_ERROR(
            CreateError<TPM2Error>(policy_session->PolicyPCR(str_pcr_map)))) {
      return WrapError<TPMError>(std::move(err), "Error creating PCR policy",
                                 TPMRetryAction::kNoRetry);
    }
    delegate = policy_session->GetDelegate();
  } else {
    default_delegate = trunks->factory->GetPasswordAuthorization("");
    delegate = default_delegate.get();
  }

  std::string tpm_plaintext;
  if (StatusChain<TPMErrorBase> err = HANDLE_TPM_COMM_ERROR(
          CreateError<TPM2Error>(trunks->tpm_utility->AsymmetricDecrypt(
              key_handle, trunks::TPM_ALG_OAEP, trunks::TPM_ALG_SHA256,
              local_data.to_string(), delegate, &tpm_plaintext)))) {
    return WrapError<TPMError>(std::move(err), "Error decrypting plaintext");
  }
  plaintext->assign(tpm_plaintext.begin(), tpm_plaintext.end());
  return nullptr;
}

StatusChain<TPMErrorBase> Tpm2Impl::SealToPcrWithAuthorization(
    const SecureBlob& plaintext,
    const SecureBlob& auth_value,
    const std::map<uint32_t, brillo::Blob>& pcr_map,
    SecureBlob* sealed_data) {
  TrunksClientContext* trunks;
  if (!GetTrunksContext(&trunks)) {
    return CreateError<TPMError>("Failed to get trunks context",
                                 TPMRetryAction::kNoRetry);
  }

  std::map<uint32_t, std::string> str_pcr_map = ToStrPcrMap(pcr_map);

  // Get the policy digest for PCR.
  std::string policy_digest;
  if (StatusChain<TPMErrorBase> err =
          HANDLE_TPM_COMM_ERROR(CreateError<TPM2Error>(
              trunks->tpm_utility->GetPolicyDigestForPcrValues(
                  str_pcr_map, true /* use_auth_value */, &policy_digest)))) {
    return WrapError<TPMError>(std::move(err), "Error getting policy digest");
  }

  std::unique_ptr<trunks::HmacSession> session =
      trunks->factory->GetHmacSession();
  if (StatusChain<TPMErrorBase> err = HANDLE_TPM_COMM_ERROR(
          CreateError<TPM2Error>(session->StartUnboundSession(true, true)))) {
    return WrapError<TPMError>(std::move(err), "Error starting hmac session");
  }

  std::string sealed_str;
  if (StatusChain<TPMErrorBase> err = HANDLE_TPM_COMM_ERROR(
          CreateError<TPM2Error>(trunks->tpm_utility->SealData(
              plaintext.to_string(), policy_digest, auth_value.to_string(),
              /*require_admin_with_policy=*/true, session->GetDelegate(),
              &sealed_str)))) {
    return WrapError<TPMError>(std::move(err),
                               "Error sealing data to PCR with authorization");
  }
  sealed_data->assign(sealed_str.begin(), sealed_str.end());

  return nullptr;
}

StatusChain<TPMErrorBase> Tpm2Impl::PreloadSealedData(
    const brillo::SecureBlob& sealed_data, ScopedKeyHandle* preload_handle) {
  if (StatusChain<TPMErrorBase> err =
          LoadWrappedKey(sealed_data, preload_handle)) {
    return WrapError<TPMError>(std::move(err), "Failed to load sealed data");
  }
  return nullptr;
}

StatusChain<TPMErrorBase> Tpm2Impl::UnsealWithAuthorization(
    std::optional<TpmKeyHandle> preload_handle,
    const SecureBlob& sealed_data,
    const SecureBlob& auth_value,
    const std::map<uint32_t, brillo::Blob>& pcr_map,
    SecureBlob* plaintext) {
  TrunksClientContext* trunks;
  if (!GetTrunksContext(&trunks)) {
    return CreateError<TPMError>("Failed to get trunks context",
                                 TPMRetryAction::kNoRetry);
  }

  std::unique_ptr<trunks::PolicySession> policy_session =
      trunks->factory->GetPolicySession();
  // Use unsalted session here, to unseal faster.
  if (StatusChain<TPMErrorBase> err =
          HANDLE_TPM_COMM_ERROR(CreateError<TPM2Error>(
              policy_session->StartUnboundSession(false, false)))) {
    return WrapError<TPMError>(std::move(err), "Error starting policy session");
  }
  if (StatusChain<TPMErrorBase> err = HANDLE_TPM_COMM_ERROR(
          CreateError<TPM2Error>(policy_session->PolicyAuthValue()))) {
    return WrapError<TPMError>(std::move(err),
                               "Error setting session to use auth_value");
  }

  std::map<uint32_t, std::string> str_pcr_map = ToStrPcrMap(pcr_map);
  if (StatusChain<TPMErrorBase> err = HANDLE_TPM_COMM_ERROR(
          CreateError<TPM2Error>(policy_session->PolicyPCR(str_pcr_map)))) {
    return WrapError<TPMError>(std::move(err), "Error in PolicyPCR");
  }
  policy_session->SetEntityAuthorizationValue(auth_value.to_string());
  std::string unsealed_data;
  if (preload_handle) {
    if (StatusChain<TPMErrorBase> err = HANDLE_TPM_COMM_ERROR(
            CreateError<TPM2Error>(trunks->tpm_utility->UnsealDataWithHandle(
                *preload_handle, policy_session->GetDelegate(),
                &unsealed_data)))) {
      return WrapError<TPMError>(std::move(err),
                                 "Error unsealing data with authorization");
    }
  } else {
    if (StatusChain<TPMErrorBase> err = HANDLE_TPM_COMM_ERROR(
            CreateError<TPM2Error>(trunks->tpm_utility->UnsealData(
                sealed_data.to_string(), policy_session->GetDelegate(),
                &unsealed_data)))) {
      return WrapError<TPMError>(std::move(err),
                                 "Error unsealing data with authorization");
    }
  }
  plaintext->assign(unsealed_data.begin(), unsealed_data.end());

  return nullptr;
}

StatusChain<TPMErrorBase> Tpm2Impl::GetPublicKeyHash(TpmKeyHandle key_handle,
                                                     SecureBlob* hash) {
  CHECK(hash);
  TrunksClientContext* trunks;
  if (!GetTrunksContext(&trunks)) {
    return CreateError<TPMError>("Failed to get trunks context",
                                 TPMRetryAction::kNoRetry);
  }
  trunks::TPMT_PUBLIC public_data;
  if (StatusChain<TPMErrorBase> err = HANDLE_TPM_COMM_ERROR(
          CreateError<TPM2Error>(trunks->tpm_utility->GetKeyPublicArea(
              key_handle, &public_data)))) {
    return WrapError<TPMError>(std::move(err), "Error getting key public area");
  }
  std::string public_modulus =
      trunks::StringFrom_TPM2B_PUBLIC_KEY_RSA(public_data.unique.rsa);
  *hash = Sha256(SecureBlob(public_modulus));
  return nullptr;
}

void Tpm2Impl::GetStatus(std::optional<TpmKeyHandle> key,
                         TpmStatusInfo* status) {
  memset(status, 0, sizeof(TpmStatusInfo));
  TrunksClientContext* trunks;
  if (!GetTrunksContext(&trunks)) {
    return;
  }
  status->this_instance_has_context = true;
  status->this_instance_has_key_handle = key.has_value();
  status->last_tpm_error = trunks->tpm_state->Initialize();
  if (status->last_tpm_error != TPM_RC_SUCCESS) {
    return;
  }
  status->can_connect = true;
  trunks::TPMT_PUBLIC public_srk;
  status->last_tpm_error = trunks->tpm_utility->GetKeyPublicArea(
      trunks::kStorageRootKey, &public_srk);
  if (status->last_tpm_error != TPM_RC_SUCCESS) {
    return;
  }
  status->can_load_srk = true;
  status->can_load_srk_public_key = true;
  status->srk_vulnerable_roca = false;

  // Check the Cryptohome key by using what we have been told.
  status->has_cryptohome_key = key.has_value();

  if (status->has_cryptohome_key) {
    // Check encryption (we don't care about the contents, just whether or not
    // there was an error)
    SecureBlob data(16);
    SecureBlob password(16);
    SecureBlob salt(8);
    SecureBlob data_out(16);
    memset(data.data(), 'A', data.size());
    memset(password.data(), 'B', password.size());
    memset(salt.data(), 'C', salt.size());
    memset(data_out.data(), 'D', data_out.size());
    SecureBlob aes_key;
    PasskeyToAesKey(password, salt, 13, &aes_key, NULL);
    if (StatusChain<TPMErrorBase> err =
            EncryptBlob(key.value(), data, aes_key, &data_out)) {
      LOG(ERROR) << __func__ << ": Failed to encrypt blob: " << err;
      return;
    }
    status->can_encrypt = true;

    // Check decryption (we don't care about the contents, just whether or not
    // there was an error)
    if (StatusChain<TPMErrorBase> err =
            DecryptBlob(key.value(), data_out, aes_key,
                        std::map<uint32_t, brillo::Blob>(), &data)) {
      LOG(ERROR) << __func__ << ": Failed to decrypt blob: " << err;
      return;
    }
    status->can_decrypt = true;
  }
}

StatusChain<hwsec::TPMErrorBase> Tpm2Impl::IsSrkRocaVulnerable(bool* result) {
  // This doesn't apply to devices with TPM 2.0.
  *result = false;
  return nullptr;
}

bool Tpm2Impl::GetDictionaryAttackInfo(int* counter,
                                       int* threshold,
                                       bool* lockout,
                                       int* seconds_remaining) {
  if (!InitializeTpmManagerUtility()) {
    LOG(ERROR) << __func__ << ": Failed to initialize |TpmManagerUtility|.";
    return false;
  }
  return tpm_manager_utility_->GetDictionaryAttackInfo(
      counter, threshold, lockout, seconds_remaining);
}

bool Tpm2Impl::ResetDictionaryAttackMitigation(
    const Blob& /* delegate_blob */, const Blob& /* delegate_secret */) {
  if (!InitializeTpmManagerUtility()) {
    LOG(ERROR) << __func__ << ": Failed to initialize |TpmManagerUtility|.";
    return false;
  }
  return tpm_manager_utility_->ResetDictionaryAttackLock();
}

void Tpm2Impl::DeclareTpmFirmwareStable() {
  TrunksClientContext* trunks;
  if (!fw_declared_stable_ && GetTrunksContext(&trunks)) {
    StatusChain<TPMErrorBase> err =
        HANDLE_TPM_COMM_ERROR(CreateError<TPM2Error>(
            trunks->tpm_utility->DeclareTpmFirmwareStable()));
    fw_declared_stable_ = (err == nullptr);
  }
}

bool Tpm2Impl::GetTrunksContext(TrunksClientContext** trunks) {
  if (has_external_trunks_context_) {
    *trunks = &external_trunks_context_;
    return true;
  }
  base::PlatformThreadId thread_id = base::PlatformThread::CurrentId();
  std::map<base::PlatformThreadId,
           std::unique_ptr<Tpm2Impl::TrunksClientContext>>::iterator iter;
  {
    base::AutoLock lock(trunks_contexts_lock_);
    iter = trunks_contexts_.find(thread_id);
    if (iter == trunks_contexts_.end()) {
      auto result = trunks_contexts_.emplace(thread_id, nullptr);
      iter = std::move(result.first);
    }
  }

  // Different elements in the same container can be modified concurrently by
  // different threads, so we don't need to lock this block.
  if (!iter->second) {
    std::unique_ptr<TrunksClientContext> new_context(new TrunksClientContext);
    new_context->factory_impl = std::make_unique<trunks::TrunksFactoryImpl>();
    if (!new_context->factory_impl->Initialize()) {
      LOG(ERROR) << "Failed to initialize trunks factory.";
      return false;
    }
    new_context->factory = new_context->factory_impl.get();
    new_context->tpm_state = new_context->factory->GetTpmState();
    new_context->tpm_utility = new_context->factory->GetTpmUtility();
    iter->second = std::move(new_context);
  }
  *trunks = iter->second.get();
  return true;
}

bool Tpm2Impl::LoadPublicKeyFromSpki(
    const Blob& public_key_spki_der,
    AsymmetricKeyUsage key_type,
    trunks::TPM_ALG_ID scheme,
    trunks::TPM_ALG_ID hash_alg,
    trunks::AuthorizationDelegate* session_delegate,
    ScopedKeyHandle* key_handle) {
  // Parse the SPKI.
  const unsigned char* asn1_ptr = public_key_spki_der.data();
  const crypto::ScopedEVP_PKEY pkey(
      d2i_PUBKEY(nullptr, &asn1_ptr, public_key_spki_der.size()));
  if (!pkey) {
    LOG(ERROR) << "Error parsing Subject Public Key Info DER";
    return false;
  }
  const crypto::ScopedRSA rsa(EVP_PKEY_get1_RSA(pkey.get()));
  if (!rsa) {
    LOG(ERROR) << "Error: non-RSA key was supplied";
    return false;
  }
  SecureBlob key_modulus(RSA_size(rsa.get()));
  const BIGNUM* n;
  const BIGNUM* e;
  RSA_get0_key(rsa.get(), &n, &e, nullptr);
  if (BN_bn2bin(n, key_modulus.data()) != key_modulus.size()) {
    LOG(ERROR) << "Error extracting public key modulus";
    return false;
  }
  constexpr BN_ULONG kInvalidBnWord = ~static_cast<BN_ULONG>(0);
  const BN_ULONG exponent_word = BN_get_word(e);
  if (exponent_word == kInvalidBnWord ||
      !base::IsValueInRangeForNumericType<uint32_t>(exponent_word)) {
    LOG(ERROR) << "Error extracting public key exponent";
    return false;
  }
  const uint32_t key_exponent = static_cast<uint32_t>(exponent_word);
  // Load the key into the TPM.
  TrunksClientContext* trunks;
  if (!GetTrunksContext(&trunks))
    return false;
  trunks::TPM_HANDLE key_handle_raw = 0;
  if (StatusChain<TPMErrorBase> err = HANDLE_TPM_COMM_ERROR(
          CreateError<TPM2Error>(trunks->tpm_utility->LoadRSAPublicKey(
              ConvertAsymmetricKeyUsage(key_type), scheme, hash_alg,
              key_modulus.to_string(), key_exponent, session_delegate,
              &key_handle_raw)))) {
    LOG(ERROR) << "Error loading public key: " << err;
    return false;
  }
  key_handle->reset(this, key_handle_raw);
  return true;
}

bool Tpm2Impl::PublicAreaToPublicKeyDER(const trunks::TPMT_PUBLIC& public_area,
                                        brillo::SecureBlob* public_key_der) {
  crypto::ScopedRSA rsa(RSA_new());
  crypto::ScopedBIGNUM e(BN_new()), n(BN_new());
  if (!rsa || !e || !n) {
    LOG(ERROR) << "Failed to allocate RSA or BIGNUM for public key.";
    return false;
  }
  if (!BN_set_word(e.get(), kDefaultTpmPublicExponent) ||
      !BN_bin2bn(public_area.unique.rsa.buffer, public_area.unique.rsa.size,
                 n.get()) ||
      !RSA_set0_key(rsa.get(), n.release(), e.release(), nullptr)) {
    LOG(ERROR) << "Failed to set up RSA.";
    return false;
  }
  int der_length = i2d_RSAPublicKey(rsa.get(), nullptr);
  if (der_length < 0) {
    LOG(ERROR) << "Failed to get DER-encoded public key length.";
    return false;
  }
  public_key_der->resize(der_length);
  unsigned char* der_buffer = public_key_der->data();
  der_length = i2d_RSAPublicKey(rsa.get(), &der_buffer);
  if (der_length < 0) {
    LOG(ERROR) << "Failed to DER-encode public key.";
    return false;
  }
  return true;
}

StatusChain<TPMErrorBase> Tpm2Impl::GetAuthValue(
    std::optional<TpmKeyHandle> key_handle,
    const SecureBlob& pass_blob,
    SecureBlob* auth_value) {
  if (!key_handle) {
    LOG(DFATAL) << "TPM2.0 needs a key_handle to get auth value.";
    return CreateError<TPMError>("TPM2.0 needs a key_handle to get auth value",
                                 TPMRetryAction::kNoRetry);
  }
  if (pass_blob.size() != kDefaultTpmRsaModulusSize / 8) {
    return CreateError<TPMError>(
        "Unexpected pass_blob size: " + std::to_string(pass_blob.size()),
        TPMRetryAction::kNoRetry);
  }

  TrunksClientContext* trunks;
  if (!GetTrunksContext(&trunks)) {
    return CreateError<TPMError>("Failed to get trunks context",
                                 TPMRetryAction::kNoRetry);
  }

  // To guarantee that pass_blob is lower that public key modulus, just set the
  // first byte to 0.
  std::string value_to_decrypt = pass_blob.to_string();
  value_to_decrypt[0] = 0;
  std::string decrypted_value;
  std::unique_ptr<trunks::AuthorizationDelegate> delegate =
      trunks->factory->GetPasswordAuthorization("");
  if (StatusChain<TPMErrorBase> err = HANDLE_TPM_COMM_ERROR(
          CreateError<TPM2Error>(trunks->tpm_utility->AsymmetricDecrypt(
              key_handle.value(), trunks::TPM_ALG_NULL, trunks::TPM_ALG_NULL,
              value_to_decrypt, delegate.get(), &decrypted_value)))) {
    return WrapError<TPMError>(std::move(err), "Error decrypting pass_blob");
  }
  *auth_value = Sha256(SecureBlob(decrypted_value));

  return nullptr;
}

StatusChain<TPMErrorBase> Tpm2Impl::GetEccAuthValue(
    std::optional<TpmKeyHandle> key_handle,
    const SecureBlob& pass_blob,
    SecureBlob* auth_value) {
  if (!key_handle) {
    LOG(DFATAL) << "TPM2.0 needs a key_handle to get ECC auth value.";
    return CreateError<TPMError>(
        "TPM2.0 needs a key_handle to get ECC auth value",
        TPMRetryAction::kNoRetry);
  }

  if (pass_blob.size() < kMinPassBlobSize) {
    return CreateError<TPMError>(
        "Unexpected pass_blob size: " + std::to_string(pass_blob.size()),
        TPMRetryAction::kNoRetry);
  }

  TrunksClientContext* trunks;
  if (!GetTrunksContext(&trunks)) {
    return CreateError<TPMError>("Failed to get trunks context",
                                 TPMRetryAction::kCommunication);
  }

  trunks::TPMS_ECC_POINT ecc_point;
  if (StatusChain<TPMErrorBase> err =
          DeriveTpmEccPointFromSeed(pass_blob, &ecc_point)) {
    return WrapError<TPMError>(std::move(err),
                               "Failed to derive TPM ECC point from ");
  }

  trunks::TPM2B_ECC_POINT in_point = trunks::Make_TPM2B_ECC_POINT(ecc_point);
  trunks::TPM2B_ECC_POINT z_point;

  std::unique_ptr<trunks::AuthorizationDelegate> delegate =
      trunks->factory->GetPasswordAuthorization("");

  if (StatusChain<TPMErrorBase> err = HANDLE_TPM_COMM_ERROR(
          CreateError<TPM2Error>(trunks->tpm_utility->ECDHZGen(
              key_handle.value(), in_point, delegate.get(), &z_point)))) {
    return WrapError<TPMError>(std::move(err), "Error doing ECDH ZGen");
  }

  *auth_value =
      Sha256(SecureBlob(StringFrom_TPM2B_ECC_PARAMETER(z_point.point.x)));

  return nullptr;
}

bool Tpm2Impl::UpdateTpmStatus(RefreshType refresh_type) {
  if (!InitializeTpmManagerUtility()) {
    LOG(ERROR) << __func__ << ": Failed to initialize |TpmManagerUtility|.";
    return false;
  }

  bool is_successful = false;
  bool has_received = false;

  // Repeats data copy into |last_tpm_manager_data_|; reasonable trade-off due
  // to low ROI to avoid that.
  const bool is_connected = tpm_manager_utility_->GetOwnershipTakenSignalStatus(
      &is_successful, &has_received, &last_tpm_manager_data_);

  // When we need explicitly query tpm status either because the signal is not
  // ready for any reason, or because the signal is not received yet so we need
  // to run it once in case the signal is sent by tpm_manager before already.
  if (refresh_type == RefreshType::FORCE_REFRESH || !is_connected ||
      !is_successful || (!has_received && shall_cache_tpm_manager_status_)) {
    // Retains |shall_cache_tpm_manager_status_| to be |true| if the signal
    // cannot be relied on (yet). Actually |!is_successful| suffices to update
    // |shall_cache_tpm_manager_status_|; by design, uses the redundancy just to
    // avoid confusion.
    shall_cache_tpm_manager_status_ &= (!is_connected || !is_successful);
    return CacheTpmManagerStatus();
  } else if (has_received) {
    is_enabled_ = true;
    is_owned_ = true;
  }
  return true;
}

bool Tpm2Impl::RemoveOwnerDependency(Tpm::TpmOwnerDependency dependency) {
  if (!InitializeTpmManagerUtility()) {
    LOG(ERROR) << __func__ << ": Failed to initialize |TpmManagerUtility|.";
    return false;
  }
  return tpm_manager_utility_->RemoveOwnerDependency(
      OwnerDependencyEnumClassToString(dependency));
}

bool Tpm2Impl::GetVersionInfo(TpmVersionInfo* version_info) {
  if (!version_info) {
    LOG(ERROR) << __func__ << "version_info is not initialized.";
    return false;
  }

  // Version info on a device never changes. Returns from cache directly if we
  // have the cache.
  if (version_info_) {
    *version_info = *version_info_;
    return true;
  }

  if (!InitializeTpmManagerUtility()) {
    LOG(ERROR) << __func__ << ": failed to initialize |TpmManagerUtility|.";
    return false;
  }

  if (!tpm_manager_utility_->GetVersionInfo(
          &version_info->family, &version_info->spec_level,
          &version_info->manufacturer, &version_info->tpm_model,
          &version_info->firmware_version, &version_info->vendor_specific)) {
    LOG(ERROR) << __func__ << ": failed to get version info from tpm_manager.";
    return false;
  }

  version_info_ = *version_info;
  return true;
}

bool Tpm2Impl::GetIFXFieldUpgradeInfo(IFXFieldUpgradeInfo* info) {
  return false;
}

bool Tpm2Impl::GetRsuDeviceId(std::string* device_id) {
  TrunksClientContext* trunks;
  if (!GetTrunksContext(&trunks)) {
    return false;
  }
  return trunks->tpm_utility->GetRsuDeviceId(device_id) == TPM_RC_SUCCESS;
}

LECredentialBackend* Tpm2Impl::GetLECredentialBackend() {
#if USE_PINWEAVER
  return &le_credential_backend_;
#else
  return nullptr;
#endif
}

SignatureSealingBackend* Tpm2Impl::GetSignatureSealingBackend() {
  return &signature_sealing_backend_;
}

cryptorecovery::RecoveryCryptoTpmBackend* Tpm2Impl::GetRecoveryCryptoBackend() {
  return &recovery_crypto_backend_;
}

bool Tpm2Impl::GetDelegate(brillo::Blob* /*blob*/,
                           brillo::Blob* /*secret*/,
                           bool* has_reset_lock_permissions) {
  LOG(WARNING) << __func__ << ": No-ops to |blob| and |secret|.";
  *has_reset_lock_permissions = true;
  return true;
}

StatusChain<TPMErrorBase> Tpm2Impl::IsDelegateBoundToPcr(bool* result) {
  *result = false;
  return nullptr;
}

bool Tpm2Impl::DelegateCanResetDACounter() {
  return true;
}

std::map<uint32_t, brillo::Blob> Tpm2Impl::GetPcrMap(
    const std::string& obfuscated_username, bool use_extended_pcr) const {
  std::map<uint32_t, brillo::Blob> pcr_map;
  if (use_extended_pcr) {
    brillo::Blob starting_value(SHA256_DIGEST_LENGTH, 0);
    brillo::Blob digest_value = Sha256(brillo::CombineBlobs(
        {starting_value, Sha256(brillo::BlobFromString(obfuscated_username))}));
    pcr_map[kTpmSingleUserPCR] = digest_value;
  } else {
    pcr_map[kTpmSingleUserPCR] = brillo::Blob(SHA256_DIGEST_LENGTH, 0);
  }

  return pcr_map;
}

}  // namespace cryptohome
