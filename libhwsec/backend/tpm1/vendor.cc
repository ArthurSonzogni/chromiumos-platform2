// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec/backend/tpm1/backend.h"

#include <cinttypes>
#include <string>
#include <utility>

#include <base/memory/free_deleter.h>
#include <base/strings/stringprintf.h>
#include <crypto/scoped_openssl_types.h>
#include <libhwsec-foundation/crypto/rsa.h>
#include <libhwsec-foundation/crypto/sha.h>
#include <libhwsec-foundation/status/status_chain_macros.h>
#include <openssl/bn.h>
#include <openssl/rsa.h>
#include <tpm_manager-client/tpm_manager/dbus-proxies.h>
#include <tpm_manager/proto_bindings/tpm_manager.pb.h>

#include "libhwsec/error/tpm1_error.h"
#include "libhwsec/error/tpm_manager_error.h"

using brillo::BlobFromString;
using brillo::BlobToString;
using hwsec_foundation::kWellKnownExponent;
using hwsec_foundation::Sha256;
using hwsec_foundation::TestRocaVulnerable;
using hwsec_foundation::status::MakeStatus;

using ScopedByteArray = std::unique_ptr<BYTE, base::FreeDeleter>;
namespace hwsec {

namespace {

StatusOr<crypto::ScopedRSA> ParseRsaFromTpmPubkeyBlob(
    overalls::Overalls& overalls, const brillo::Blob& pubkey) {
  // Parse the serialized TPM_PUBKEY.
  brillo::Blob pubkey_copy = pubkey;
  uint64_t offset = 0;
  TPM_PUBKEY parsed = {};  // Zero initialize.

  RETURN_IF_ERROR(
      MakeStatus<TPM1Error>(overalls.Orspi_UnloadBlob_PUBKEY_s(
          &offset, pubkey_copy.data(), pubkey_copy.size(), &parsed)))
      .WithStatus<TPMError>("Failed to call Orspi_UnloadBlob_PUBKEY_s");

  ScopedByteArray scoped_key(parsed.pubKey.key);
  ScopedByteArray scoped_parms(parsed.algorithmParms.parms);

  if (offset != pubkey.size()) {
    return MakeStatus<TPMError>("Found garbage data after the TPM_PUBKEY",
                                TPMRetryAction::kNoRetry);
  }

  uint64_t parms_offset = 0;
  TPM_RSA_KEY_PARMS parms = {};  // Zero initialize.

  RETURN_IF_ERROR(
      MakeStatus<TPM1Error>(overalls.Orspi_UnloadBlob_RSA_KEY_PARMS_s(
          &parms_offset, parsed.algorithmParms.parms,
          parsed.algorithmParms.parmSize, &parms)))
      .WithStatus<TPMError>("Failed to call Orspi_UnloadBlob_RSA_KEY_PARMS_s");

  ScopedByteArray scoped_exponent(parms.exponent);

  if (parms_offset != parsed.algorithmParms.parmSize) {
    return MakeStatus<TPMError>(
        "Found garbage data after the TPM_PUBKEY algorithm params",
        TPMRetryAction::kNoRetry);
  }

  // Get the public exponent.
  crypto::ScopedRSA rsa(RSA_new());
  crypto::ScopedBIGNUM e(BN_new()), n(BN_new());
  if (!rsa || !e || !n) {
    return MakeStatus<TPMError>("Failed to create RSA or BIGNUM",
                                TPMRetryAction::kNoRetry);
  }
  if (!parms.exponentSize) {
    if (!BN_set_word(e.get(), kWellKnownExponent)) {
      return MakeStatus<TPMError>(
          "Failed to set BN exponent to WellKnownExponent",
          TPMRetryAction::kNoRetry);
    }
  } else {
    if (!BN_bin2bn(parms.exponent, parms.exponentSize, e.get())) {
      return MakeStatus<TPMError>("Failed to load BN exponent from TPM_PUBKEY",
                                  TPMRetryAction::kNoRetry);
    }
  }

  // Get the modulus.
  if (!BN_bin2bn(parsed.pubKey.key, parsed.pubKey.keyLength, n.get())) {
    return MakeStatus<TPMError>("Failed to load BN modulus from TPM_PUBKEY",
                                TPMRetryAction::kNoRetry);
  }

  if (!RSA_set0_key(rsa.get(), n.release(), e.release(), nullptr)) {
    return MakeStatus<TPMError>("Failed to set parameters for RSA",
                                TPMRetryAction::kNoRetry);
  }

  return rsa;
}

}  // namespace

using VendorTpm1 = BackendTpm1::VendorTpm1;

Status VendorTpm1::EnsureVersionInfo() {
  if (version_info_.has_value()) {
    return OkStatus();
  }

  tpm_manager::GetVersionInfoRequest request;
  tpm_manager::GetVersionInfoReply reply;

  if (brillo::ErrorPtr err; !backend_.proxy_.GetTpmManager().GetVersionInfo(
          request, &reply, &err, Proxy::kDefaultDBusTimeoutMs)) {
    return MakeStatus<TPMError>(TPMRetryAction::kCommunication)
        .Wrap(std::move(err));
  }

  RETURN_IF_ERROR(MakeStatus<TPMManagerError>(reply.status()));

  version_info_ = std::move(reply);
  return OkStatus();
}

StatusOr<uint32_t> VendorTpm1::GetFamily() {
  RETURN_IF_ERROR(EnsureVersionInfo());

  return version_info_->family();
}

StatusOr<uint64_t> VendorTpm1::GetSpecLevel() {
  RETURN_IF_ERROR(EnsureVersionInfo());

  return version_info_->spec_level();
}

StatusOr<uint32_t> VendorTpm1::GetManufacturer() {
  RETURN_IF_ERROR(EnsureVersionInfo());

  return version_info_->manufacturer();
}

StatusOr<uint32_t> VendorTpm1::GetTpmModel() {
  RETURN_IF_ERROR(EnsureVersionInfo());

  return version_info_->tpm_model();
}

StatusOr<uint64_t> VendorTpm1::GetFirmwareVersion() {
  RETURN_IF_ERROR(EnsureVersionInfo());

  return version_info_->firmware_version();
}

StatusOr<brillo::Blob> VendorTpm1::GetVendorSpecific() {
  RETURN_IF_ERROR(EnsureVersionInfo());

  return brillo::BlobFromString(version_info_->vendor_specific());
}

StatusOr<int32_t> VendorTpm1::GetFingerprint() {
  RETURN_IF_ERROR(EnsureVersionInfo());

  // The exact encoding doesn't matter as long as its unambiguous, stable and
  // contains all information present in the version fields.
  std::string encoded_parameters = base::StringPrintf(
      "%08" PRIx32 "%016" PRIx64 "%08" PRIx32 "%08" PRIx32 "%016" PRIx64
      "%016zx",
      version_info_->family(), version_info_->spec_level(),
      version_info_->manufacturer(), version_info_->tpm_model(),
      version_info_->firmware_version(),
      version_info_->vendor_specific().size());
  encoded_parameters.append(version_info_->vendor_specific());

  brillo::Blob hash = Sha256(brillo::BlobFromString(encoded_parameters));

  // Return the first 31 bits from |hash|.
  uint32_t result = static_cast<uint32_t>(hash[0]) |
                    static_cast<uint32_t>(hash[1]) << 8 |
                    static_cast<uint32_t>(hash[2]) << 16 |
                    static_cast<uint32_t>(hash[3]) << 24;
  return result & 0x7fffffff;
}

StatusOr<bool> VendorTpm1::IsSrkRocaVulnerable() {
  ASSIGN_OR_RETURN(
      ScopedKey srk,
      backend_.key_management_.GetPersistentKey(
          Backend::KeyManagement::PersistentKeyType::kStorageRootKey));

  ASSIGN_OR_RETURN(const KeyTpm1& srk_data,
                   backend_.key_management_.GetKeyData(srk.GetKey()));

  overalls::Overalls& overalls = backend_.overall_context_.overalls;

  ASSIGN_OR_RETURN(
      const crypto::ScopedRSA& public_srk,
      ParseRsaFromTpmPubkeyBlob(overalls, srk_data.cache.pubkey_blob),
      _.WithStatus<TPMError>("Failed to parse RSA public key"));

  const BIGNUM* n = nullptr;
  RSA_get0_key(public_srk.get(), &n, nullptr, nullptr);

  return TestRocaVulnerable(n);
}

StatusOr<brillo::Blob> VendorTpm1::GetRsuDeviceId() {
  return MakeStatus<TPMError>("Unsupported command", TPMRetryAction::kNoRetry);
}

StatusOr<brillo::Blob> VendorTpm1::GetIFXFieldUpgradeInfo() {
  return MakeStatus<TPMError>("Unimplemented", TPMRetryAction::kNoRetry);
}

Status VendorTpm1::DeclareTpmFirmwareStable() {
  // No-op on TPM1.2
  return OkStatus();
}

StatusOr<brillo::Blob> VendorTpm1::SendRawCommand(const brillo::Blob& command) {
  return MakeStatus<TPMError>("Unimplemented", TPMRetryAction::kNoRetry);
}

}  // namespace hwsec
