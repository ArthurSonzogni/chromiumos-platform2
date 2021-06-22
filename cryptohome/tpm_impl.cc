// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Contains the implementation of class Tpm

#include "cryptohome/tpm_impl.h"

#include <stdio.h>
#include <stdlib.h>

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <arpa/inet.h>
#include <base/check.h>
#include <base/check_op.h>
#include <base/logging.h>
#include <base/memory/free_deleter.h>
#include <base/notreached.h>
#include <base/stl_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/stringprintf.h>
#include <base/threading/platform_thread.h>
#include <base/time/time.h>
#include <base/values.h>
#include <crypto/libcrypto-compat.h>
#include <crypto/scoped_openssl_types.h>
#include <libhwsec/overalls/overalls_api.h>
#include <libhwsec/error/tpm1_error.h>
#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>
#include <tpm_manager-client/tpm_manager/dbus-constants.h>
#include <trousers/scoped_tss_type.h>
#include <trousers/tss.h>
#include <trousers/trousers.h>  // NOLINT(build/include_alpha) - needs tss.h

#include "cryptohome/crypto/aes.h"
#include "cryptohome/crypto/rsa.h"
#include "cryptohome/crypto/secure_blob_util.h"
#include "cryptohome/crypto/sha.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/tpm1_static_utils.h"
#include "cryptohome/tpm_metrics.h"

#define TPM_LOG(severity, result) \
  LOG(severity) << ::cryptohome::FormatTrousersErrorCode(result) << ": "

using base::PlatformThread;
using brillo::Blob;
using brillo::BlobFromString;
using brillo::CombineBlobs;
using brillo::SecureBlob;
using hwsec::error::TPM1Error;
using hwsec::error::TPMError;
using hwsec::error::TPMErrorBase;
using hwsec::error::TPMRetryAction;
using hwsec::overalls::GetOveralls;
using hwsec_foundation::error::CreateError;
using hwsec_foundation::error::CreateErrorWrap;
using trousers::ScopedTssContext;
using trousers::ScopedTssKey;
using trousers::ScopedTssMemory;
using trousers::ScopedTssNvStore;
using trousers::ScopedTssObject;
using trousers::ScopedTssPcrs;
using trousers::ScopedTssPolicy;

namespace cryptohome {

// Returns the corresponding TpmResult enum value to be used to report a
// "Cryptohome.TpmResults" histogram sample.
TpmResult GetTpmResultSample(TSS_RESULT result);

namespace {

typedef std::unique_ptr<BYTE, base::FreeDeleter> ScopedByteArray;

// The DER encoding of SHA-256 DigestInfo as defined in PKCS #1.
const unsigned char kSha256DigestInfo[] = {
    0x30, 0x31, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01,
    0x65, 0x03, 0x04, 0x02, 0x01, 0x05, 0x00, 0x04, 0x20};

// This is the well known UUID present in TPM1.2 implemenations. It is used
// to load the cryptohome key into a TPM1.2 in a legacy path.
const TSS_UUID kCryptohomeWellKnownUuid = {0x0203040b, 0, 0,
                                           0,          0, {0, 9, 8, 1, 0, 3}};

// Creates a DER encoded RSA public key given a serialized TPM_PUBKEY.
//
// Parameters
//   public_key - A serialized TPM_PUBKEY as returned by Tspi_Key_GetPubKey.
//   public_key_der - The same public key in DER encoded form.
bool ConvertPublicKeyToDER(const SecureBlob& public_key,
                           SecureBlob* public_key_der) {
  crypto::ScopedRSA rsa =
      ParseRsaFromTpmPubkeyBlob(Blob(public_key.begin(), public_key.end()));
  if (!rsa) {
    return false;
  }

  int der_length = i2d_RSAPublicKey(rsa.get(), NULL);
  if (der_length < 0) {
    LOG(ERROR) << "Failed to DER-encode public key.";
    return false;
  }
  public_key_der->resize(der_length);
  unsigned char* der_buffer = public_key_der->data();
  der_length = i2d_RSAPublicKey(rsa.get(), &der_buffer);
  if (der_length < 0) {
    LOG(ERROR) << "Failed to DER-encode public key.";
    return false;
  }
  public_key_der->resize(der_length);
  return true;
}

std::string OwnerDependencyEnumClassToString(
    Tpm::TpmOwnerDependency dependency) {
  switch (dependency) {
    case Tpm::TpmOwnerDependency::kInstallAttributes:
      return tpm_manager::kTpmOwnerDependency_Nvram;
    case Tpm::TpmOwnerDependency::kAttestation:
      return tpm_manager::kTpmOwnerDependency_Attestation;
  }
  NOTREACHED() << __func__ << ": Unexpected enum class value: "
               << static_cast<int>(dependency);
  return "";
}

}  // namespace

const unsigned char kDefaultSrkAuth[] = {};
const unsigned int kDefaultTpmRsaKeyFlag = TSS_KEY_SIZE_2048;
const unsigned int kDefaultDiscardableWrapPasswordLength = 32;

const char* kWellKnownSrkTmp = "1234567890";
const unsigned int kTpmConnectRetries = 10;
const unsigned int kTpmConnectIntervalMs = 100;
const unsigned int kTpmPCRLocality = 1;
const int kDelegateSecretSize = 20;
const size_t kPCRExtensionSize = 20;  // SHA-1 digest size.

// This error is returned when an attempt is made to use the SRK but it does not
// yet exist because the TPM has not been owned.
const TSS_RESULT kKeyNotFoundError = (TSS_E_PS_KEY_NOTFOUND | TSS_LAYER_TCS);

TpmImpl::TpmImpl()
    : srk_auth_(kDefaultSrkAuth, kDefaultSrkAuth + sizeof(kDefaultSrkAuth)),
      owner_password_() {
  TSS_HCONTEXT context_handle = ConnectContext();
  if (context_handle) {
    tpm_context_.reset(0, context_handle);
  }
}

TpmImpl::~TpmImpl() {}

void TpmImpl::SetTpmManagerUtilityForTesting(
    tpm_manager::TpmManagerUtility* tpm_manager_utility) {
  tpm_manager_utility_ = tpm_manager_utility;
}

TSS_HCONTEXT TpmImpl::ConnectContext() {
  TSS_HCONTEXT context_handle = 0;
  if (TPM1Error err = OpenAndConnectTpm(&context_handle)) {
    LOG(ERROR) << "Failed to OpenAndConnectTpm: " << *err;
    return 0;
  }
  return context_handle;
}

bool TpmImpl::ConnectContextAsOwner(TSS_HCONTEXT* context, TSS_HTPM* tpm) {
  *context = 0;
  *tpm = 0;
  SecureBlob owner_password;
  if (!GetOwnerPassword(&owner_password)) {
    LOG(ERROR) << "ConnectContextAsOwner requires an owner password";
    return false;
  }

  if (!IsOwned()) {
    LOG(ERROR) << "ConnectContextAsOwner: TPM is unowned";
    return false;
  }

  if ((*context = ConnectContext()) == 0) {
    LOG(ERROR) << "ConnectContextAsOwner: Could not open the TPM";
    return false;
  }

  if (!GetTpmWithAuth(*context, owner_password, tpm)) {
    LOG(ERROR) << "ConnectContextAsOwner: failed to authorize as the owner";
    Tspi_Context_Close(*context);
    *context = 0;
    *tpm = 0;
    return false;
  }
  return true;
}

bool TpmImpl::ConnectContextAsUser(TSS_HCONTEXT* context, TSS_HTPM* tpm) {
  *context = 0;
  *tpm = 0;
  if ((*context = ConnectContext()) == 0) {
    LOG(ERROR) << "ConnectContextAsUser: Could not open the TPM";
    return false;
  }
  if (!GetTpm(*context, tpm)) {
    LOG(ERROR) << "ConnectContextAsUser: failed to get a TPM object";
    Tspi_Context_Close(*context);
    *context = 0;
    *tpm = 0;
    return false;
  }
  return true;
}

bool TpmImpl::ConnectContextAsDelegate(const Blob& delegate_blob,
                                       const Blob& delegate_secret,
                                       TSS_HCONTEXT* context,
                                       TSS_HTPM* tpm_handle) {
  *context = 0;
  *tpm_handle = 0;
  if (!IsOwned()) {
    LOG(ERROR) << "ConnectContextAsDelegate: TPM is unowned.";
    return false;
  }
  if ((*context = ConnectContext()) == 0) {
    LOG(ERROR) << "ConnectContextAsDelegate: Could not open the TPM.";
    return false;
  }
  if (!GetTpmWithDelegation(*context, delegate_blob, delegate_secret,
                            tpm_handle)) {
    LOG(ERROR) << "ConnectContextAsDelegate: Failed to authorize.";
    Tspi_Context_Close(*context);
    *context = 0;
    *tpm_handle = 0;
    return false;
  }
  return true;
}

void TpmImpl::GetStatus(base::Optional<TpmKeyHandle> key_handle,
                        TpmStatusInfo* status) {
  memset(status, 0, sizeof(TpmStatusInfo));
  status->this_instance_has_context = (tpm_context_.value() != 0);
  status->this_instance_has_key_handle = key_handle.has_value();
  ScopedTssContext context_handle;
  // Check if we can connect
  if (TPM1Error err = OpenAndConnectTpm(context_handle.ptr())) {
    status->last_tpm_error = err->ErrorCode();
    return;
  }
  status->can_connect = true;

  // Check the Storage Root Key
  ScopedTssKey srk_handle(context_handle);
  if (TPM1Error err = LoadSrk(context_handle, srk_handle.ptr())) {
    status->last_tpm_error = err->ErrorCode();
    return;
  }
  status->can_load_srk = true;

  // Check the SRK public key
  unsigned int public_srk_size;
  ScopedTssMemory public_srk_bytes(context_handle);
  if (auto err = CreateError<TPM1Error>(Tspi_Key_GetPubKey(
          srk_handle, &public_srk_size, public_srk_bytes.ptr()))) {
    LOG(ERROR) << "Failed to get public key: " << *err;
    status->last_tpm_error = err->ErrorCode();
    return;
  }
  status->can_load_srk_public_key = true;

  // Perform ROCA vulnerability check.
  crypto::ScopedRSA public_srk = ParseRsaFromTpmPubkeyBlob(Blob(
      public_srk_bytes.value(), public_srk_bytes.value() + public_srk_size));

  if (public_srk) {
    const BIGNUM* n;
    RSA_get0_key(public_srk.get(), &n, nullptr, nullptr);
    status->srk_vulnerable_roca = TestRocaVulnerable(n);
  } else {
    status->srk_vulnerable_roca = false;
  }

  // Check the Cryptohome key by using what we have been told.
  status->has_cryptohome_key =
      (tpm_context_.value() != 0) && key_handle.has_value();

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
    SecureBlob key;
    PasskeyToAesKey(password, salt, 13, &key, NULL);
    if (TPMErrorBase err =
            EncryptBlob(key_handle.value(), data, key, &data_out)) {
      LOG(ERROR) << __func__ << ": Failed to encrypt blob: " << *err;
      return;
    }
    status->can_encrypt = true;

    // Check decryption (we don't care about the contents, just whether or not
    // there was an error)
    if (TPMErrorBase err =
            DecryptBlob(key_handle.value(), data_out, key,
                        std::map<uint32_t, std::string>(), &data)) {
      LOG(ERROR) << __func__ << ": Failed to decrypt blob: " << *err;
      return;
    }
    status->can_decrypt = true;
  }
}

base::Optional<bool> TpmImpl::IsSrkRocaVulnerable() {
  if (!tpm_context_)
    return base::nullopt;
  ScopedTssKey srk_handle(tpm_context_);
  if (TPM1Error err = LoadSrk(tpm_context_, srk_handle.ptr())) {
    return base::nullopt;
  }
  unsigned public_srk_size;
  ScopedTssMemory public_srk_bytes(tpm_context_);
  if (auto err = CreateError<TPM1Error>(Tspi_Key_GetPubKey(
          srk_handle, &public_srk_size, public_srk_bytes.ptr()))) {
    LOG(ERROR) << "Failed to get public key: " << *err;
    return base::nullopt;
  }
  crypto::ScopedRSA public_srk = ParseRsaFromTpmPubkeyBlob(Blob(
      public_srk_bytes.value(), public_srk_bytes.value() + public_srk_size));
  if (!public_srk)
    return base::nullopt;

  const BIGNUM* n = nullptr;
  RSA_get0_key(public_srk.get(), &n, nullptr, nullptr);
  return TestRocaVulnerable(n);
}

bool TpmImpl::GetDictionaryAttackInfo(int* counter,
                                      int* threshold,
                                      bool* lockout,
                                      int* seconds_remaining) {
  if (!InitializeTpmManagerUtility()) {
    LOG(ERROR) << __func__ << ": failed to initialize |TpmManagerUtility|.";
    return false;
  }
  return tpm_manager_utility_->GetDictionaryAttackInfo(
      counter, threshold, lockout, seconds_remaining);
}

bool TpmImpl::ResetDictionaryAttackMitigation(const brillo::Blob&,
                                              const brillo::Blob&) {
  if (!InitializeTpmManagerUtility()) {
    LOG(ERROR) << __func__ << ": failed to initialize |TpmManagerUtility|.";
    return false;
  }
  return tpm_manager_utility_->ResetDictionaryAttackLock();
}

bool TpmImpl::CreatePolicyWithRandomPassword(TSS_HCONTEXT context_handle,
                                             TSS_FLAG policy_type,
                                             TSS_HPOLICY* policy_handle) {
  trousers::ScopedTssPolicy local_policy(context_handle);
  if (auto err = CreateError<TPM1Error>(
          Tspi_Context_CreateObject(context_handle, TSS_OBJECT_TYPE_POLICY,
                                    policy_type, local_policy.ptr()))) {
    LOG(ERROR) << "Error creating policy object: " << *err;
    return false;
  }
  auto migration_password =
      CreateSecureRandomBlob(kDefaultDiscardableWrapPasswordLength);
  if (auto err = CreateError<TPM1Error>(Tspi_Policy_SetSecret(
          local_policy, TSS_SECRET_MODE_PLAIN, migration_password.size(),
          migration_password.data()))) {
    LOG(ERROR) << "Error setting policy password: " << *err;
    return false;
  }
  *policy_handle = local_policy.release();
  return true;
}

bool TpmImpl::CreateRsaPublicKeyObject(TSS_HCONTEXT context_handle,
                                       const Blob& key_modulus,
                                       TSS_FLAG key_flags,
                                       UINT32 signature_scheme,
                                       UINT32 encryption_scheme,
                                       TSS_HKEY* key_handle) {
  ScopedTssKey local_key(context_handle);
  if (auto err = CreateError<TPM1Error>(
          Tspi_Context_CreateObject(context_handle, TSS_OBJECT_TYPE_RSAKEY,
                                    key_flags, local_key.ptr()))) {
    LOG(ERROR) << __func__ << ": Error creating the key object: " << *err;
    return false;
  }

  if (auto err = CreateError<TPM1Error>(Tspi_SetAttribData(
          local_key, TSS_TSPATTRIB_RSAKEY_INFO,
          TSS_TSPATTRIB_KEYINFO_RSA_MODULUS, key_modulus.size(),
          const_cast<BYTE*>(key_modulus.data())))) {
    LOG(ERROR) << __func__ << ": Error setting the key modulus: " << *err;
    return false;
  }
  if (signature_scheme != TSS_SS_NONE) {
    if (auto err = CreateError<TPM1Error>(Tspi_SetAttribUint32(
            local_key, TSS_TSPATTRIB_KEY_INFO, TSS_TSPATTRIB_KEYINFO_SIGSCHEME,
            signature_scheme))) {
      LOG(ERROR) << __func__
                 << ": Error setting the key signing scheme: " << *err;
      return false;
    }
  }
  if (encryption_scheme != TSS_ES_NONE) {
    if (auto err = CreateError<TPM1Error>(Tspi_SetAttribUint32(
            local_key, TSS_TSPATTRIB_KEY_INFO, TSS_TSPATTRIB_KEYINFO_ENCSCHEME,
            encryption_scheme))) {
      LOG(ERROR) << __func__
                 << ": Error setting the key encryption scheme: " << *err;
      return false;
    }
  }
  *key_handle = local_key.release();
  return true;
}

TPM1Error TpmImpl::OpenAndConnectTpm(TSS_HCONTEXT* context_handle) {
  ScopedTssContext local_context_handle;
  if (auto err = CreateError<TPM1Error>(
          Tspi_Context_Create(local_context_handle.ptr()))) {
    LOG(ERROR) << "Error calling Tspi_Context_Create: " << *err;
    return err;
  }

  for (unsigned int i = 0; i < kTpmConnectRetries; i++) {
    if (auto err = CreateError<TPM1Error>(
            GetOveralls()->Ospi_Context_Connect(local_context_handle, NULL))) {
      // If there was a communications failure, try sleeping a bit here, it may
      // be that tcsd is still starting.
      if (err->ToTPMRetryAction() == TPMRetryAction::kCommunication &&
          i + 1 != kTpmConnectRetries) {
        PlatformThread::Sleep(
            base::TimeDelta::FromMilliseconds(kTpmConnectIntervalMs));

      } else {
        LOG(ERROR) << "Error calling Tspi_Context_Connect: " << *err;
        return err;
      }
    } else {
      break;
    }
  }

  *context_handle = local_context_handle.release();
  return nullptr;
}

TPMErrorBase TpmImpl::GetPublicKeyHash(TpmKeyHandle key_handle,
                                       SecureBlob* hash) {
  SecureBlob pubkey;
  if (TPM1Error err =
          GetPublicKeyBlob(tpm_context_.value(), key_handle, &pubkey)) {
    return CreateErrorWrap<TPMError>(std::move(err),
                                     "Failed to get TPM public key hash");
  }
  *hash = Sha1(pubkey);
  return nullptr;
}

TPMErrorBase TpmImpl::EncryptBlob(TpmKeyHandle key_handle,
                                  const SecureBlob& plaintext,
                                  const SecureBlob& key,
                                  SecureBlob* ciphertext) {
  TSS_FLAG init_flags = TSS_ENCDATA_SEAL;
  ScopedTssKey enc_handle(tpm_context_.value());
  if (auto err = CreateError<TPM1Error>(Tspi_Context_CreateObject(
          tpm_context_.value(), TSS_OBJECT_TYPE_ENCDATA, init_flags,
          enc_handle.ptr()))) {
    return CreateErrorWrap<TPMError>(std::move(err),
                                     "Error calling Tspi_Context_CreateObject");
  }

  // TODO(fes): Check RSA key modulus size, return an error or block input

  if (auto err = CreateError<TPM1Error>(
          Tspi_Data_Bind(enc_handle, key_handle, plaintext.size(),
                         const_cast<BYTE*>(plaintext.data())))) {
    return CreateErrorWrap<TPMError>(std::move(err),
                                     "Error calling Tspi_Data_Bind");
  }

  SecureBlob enc_data_blob;
  if (TPM1Error err = GetDataAttribute(
          tpm_context_.value(), enc_handle, TSS_TSPATTRIB_ENCDATA_BLOB,
          TSS_TSPATTRIB_ENCDATABLOB_BLOB, &enc_data_blob)) {
    return CreateErrorWrap<TPMError>(std::move(err),
                                     "Failed to read encrypted blob");
  }
  if (!ObscureRsaMessage(enc_data_blob, key, ciphertext)) {
    return CreateError<TPMError>("Error obscuring message",
                                 TPMRetryAction::kNoRetry);
  }
  return nullptr;
}
TPMErrorBase TpmImpl::DecryptBlob(
    TpmKeyHandle key_handle,
    const SecureBlob& ciphertext,
    const SecureBlob& key,
    const std::map<uint32_t, std::string>& pcr_map,
    SecureBlob* plaintext) {
  SecureBlob local_data;
  if (!UnobscureRsaMessage(ciphertext, key, &local_data)) {
    return CreateError<TPMError>("Error unobscureing message",
                                 TPMRetryAction::kNoRetry);
  }

  TSS_FLAG init_flags = TSS_ENCDATA_SEAL;
  ScopedTssKey enc_handle(tpm_context_.value());
  if (auto err = CreateError<TPM1Error>(Tspi_Context_CreateObject(
          tpm_context_.value(), TSS_OBJECT_TYPE_ENCDATA, init_flags,
          enc_handle.ptr()))) {
    return CreateErrorWrap<TPMError>(std::move(err),
                                     "Error calling Tspi_Context_CreateObject");
  }

  if (auto err = CreateError<TPM1Error>(
          Tspi_SetAttribData(enc_handle, TSS_TSPATTRIB_ENCDATA_BLOB,
                             TSS_TSPATTRIB_ENCDATABLOB_BLOB, local_data.size(),
                             local_data.data()))) {
    return CreateErrorWrap<TPMError>(std::move(err),
                                     "Error calling Tspi_SetAttribData");
  }

  ScopedTssMemory dec_data(tpm_context_.value());
  UINT32 dec_data_length = 0;
  if (auto err = CreateError<TPM1Error>(Tspi_Data_Unbind(
          enc_handle, key_handle, &dec_data_length, dec_data.ptr()))) {
    return CreateErrorWrap<TPMError>(std::move(err),
                                     "Error calling Tspi_Data_Unbind");
  }

  plaintext->resize(dec_data_length);
  memcpy(plaintext->data(), dec_data.value(), dec_data_length);
  brillo::SecureClearBytes(dec_data.value(), dec_data_length);

  return nullptr;
}

bool TpmImpl::SetAuthValue(TSS_HCONTEXT context_handle,
                           ScopedTssKey* enc_handle,
                           TSS_HTPM tpm_handle,
                           const SecureBlob& auth_value) {
  // Create the enc_handle.
  if (auto err = CreateError<TPM1Error>(
          Tspi_Context_CreateObject(context_handle, TSS_OBJECT_TYPE_ENCDATA,
                                    TSS_ENCDATA_SEAL, enc_handle->ptr()))) {
    LOG(ERROR) << "Error calling Tspi_Context_CreateObject: " << *err;
    return false;
  }

  // Get the TPM usage policy object and set the auth_value.
  TSS_HPOLICY tpm_usage_policy;
  if (auto err = CreateError<TPM1Error>(Tspi_GetPolicyObject(
          tpm_handle, TSS_POLICY_USAGE, &tpm_usage_policy))) {
    LOG(ERROR) << "Error calling Tspi_GetPolicyObject: " << *err;
    return false;
  }
  if (auto err = CreateError<TPM1Error>(Tspi_Policy_SetSecret(
          tpm_usage_policy, TSS_SECRET_MODE_PLAIN, auth_value.size(),
          const_cast<BYTE*>(auth_value.data())))) {
    LOG(ERROR) << "Error calling Tspi_Policy_SetSecret: " << *err;
    return false;
  }

  if (auto err = CreateError<TPM1Error>(
          Tspi_Policy_AssignToObject(tpm_usage_policy, *enc_handle))) {
    LOG(ERROR) << "Error calling Tspi_Policy_AssignToObject: " << *err;
    return false;
  }

  return true;
}

TPMErrorBase TpmImpl::SealToPcrWithAuthorization(
    const SecureBlob& plaintext,
    const SecureBlob& auth_value,
    const std::map<uint32_t, std::string>& pcr_map,
    SecureBlob* sealed_data) {
  ScopedTssContext context_handle;
  TSS_HTPM tpm_handle;
  if (!ConnectContextAsUser(context_handle.ptr(), &tpm_handle)) {
    return CreateError<TPMError>("Failed to connect to the TPM",
                                 TPMRetryAction::kCommunication);
  }
  // Load the Storage Root Key.
  ScopedTssKey srk_handle(context_handle);
  if (TPM1Error err = LoadSrk(context_handle, srk_handle.ptr())) {
    return CreateErrorWrap<TPMError>(std::move(err), "Failed to load SRK",
                                     TPMRetryAction::kNoRetry);
  }

  // Create a PCRS object.
  ScopedTssPcrs pcrs_handle(context_handle);
  if (auto err = CreateError<TPM1Error>(
          Tspi_Context_CreateObject(context_handle, TSS_OBJECT_TYPE_PCRS,
                                    TSS_PCRS_STRUCT_INFO, pcrs_handle.ptr()))) {
    return CreateErrorWrap<TPMError>(std::move(err),
                                     "Error calling Tspi_Context_CreateObject",
                                     TPMRetryAction::kNoRetry);
  }

  // Process the data from pcr_map.
  for (const auto& map_pair : pcr_map) {
    uint32_t pcr_index = map_pair.first;
    const std::string& digest = map_pair.second;
    if (digest.empty()) {
      UINT32 pcr_len = 0;
      ScopedTssMemory pcr_value(context_handle);
      if (auto err = CreateError<TPM1Error>(Tspi_TPM_PcrRead(
              tpm_handle, pcr_index, &pcr_len, pcr_value.ptr()))) {
        return CreateErrorWrap<TPMError>(std::move(err),
                                         "Could not read PCR value");
      }
      Tspi_PcrComposite_SetPcrValue(pcrs_handle, pcr_index, pcr_len,
                                    pcr_value.value());
    } else {
      Tspi_PcrComposite_SetPcrValue(
          pcrs_handle, pcr_index, digest.size(),
          reinterpret_cast<BYTE*>(const_cast<char*>(digest.data())));
    }
  }

  ScopedTssKey enc_handle(context_handle);
  if (!SetAuthValue(context_handle, &enc_handle, tpm_handle, auth_value)) {
    context_handle.reset();
    return CreateError<TPMError>("Failed to SetAuthValue",
                                 TPMRetryAction::kNoRetry);
  }

  // Seal the given value with the SRK.
  if (auto err = CreateError<TPM1Error>(
          Tspi_Data_Seal(enc_handle, srk_handle, plaintext.size(),
                         const_cast<BYTE*>(plaintext.data()), pcrs_handle))) {
    return CreateErrorWrap<TPMError>(std::move(err),
                                     "Error calling Tspi_Data_Seal",
                                     TPMRetryAction::kNoRetry);
  }

  // Extract the sealed value.
  ScopedTssMemory enc_data(context_handle);
  UINT32 enc_data_length = 0;
  if (auto err = CreateError<TPM1Error>(Tspi_GetAttribData(
          enc_handle, TSS_TSPATTRIB_ENCDATA_BLOB,
          TSS_TSPATTRIB_ENCDATABLOB_BLOB, &enc_data_length, enc_data.ptr()))) {
    return CreateErrorWrap<TPMError>(std::move(err),
                                     "Error calling Tspi_GetAttribData",
                                     TPMRetryAction::kNoRetry);
  }
  sealed_data->assign(&enc_data.value()[0], &enc_data.value()[enc_data_length]);

  return nullptr;
}

TPMErrorBase TpmImpl::PreloadSealedData(const brillo::SecureBlob& sealed_data,
                                        ScopedKeyHandle* preload_handle) {
  // No effect for TPM 1.2.
  return nullptr;
}

TPMErrorBase TpmImpl::UnsealWithAuthorization(
    base::Optional<TpmKeyHandle> preload_handle,
    const SecureBlob& sealed_data,
    const SecureBlob& auth_value,
    const std::map<uint32_t, std::string>& pcr_map,
    SecureBlob* plaintext) {
  if (preload_handle) {
    LOG(DFATAL) << "TPM1.2 doesn't support preload_handle.";
    return CreateError<TPMError>("TPM1.2 doesn't support preload_handle",
                                 TPMRetryAction::kNoRetry);
  }

  ScopedTssContext context_handle;
  TSS_HTPM tpm_handle;
  if (!ConnectContextAsUser(context_handle.ptr(), &tpm_handle)) {
    return CreateError<TPMError>("Failed to connect to the TPM",
                                 TPMRetryAction::kCommunication);
  }
  // Load the Storage Root Key.
  ScopedTssKey srk_handle(context_handle);
  if (TPM1Error err = LoadSrk(context_handle, srk_handle.ptr())) {
    return CreateErrorWrap<TPMError>(std::move(err), "Failed to load SRK",
                                     TPMRetryAction::kNoRetry);
  }

  // Create an ENCDATA object with the sealed value.
  ScopedTssKey enc_handle(context_handle);
  if (!SetAuthValue(context_handle, &enc_handle, tpm_handle, auth_value)) {
    context_handle.reset();
    return CreateError<TPMError>("Failed to SetAuthValue",
                                 TPMRetryAction::kNoRetry);
  }

  if (auto err = CreateError<TPM1Error>(
          Tspi_SetAttribData(enc_handle, TSS_TSPATTRIB_ENCDATA_BLOB,
                             TSS_TSPATTRIB_ENCDATABLOB_BLOB, sealed_data.size(),
                             const_cast<BYTE*>(sealed_data.data())))) {
    return CreateErrorWrap<TPMError>(std::move(err),
                                     "Error calling Tspi_SetAttribData");
  }

  // Unseal using the SRK.
  ScopedTssMemory dec_data(context_handle);
  UINT32 dec_data_length = 0;
  if (auto err = CreateError<TPM1Error>(Tspi_Data_Unseal(
          enc_handle, srk_handle, &dec_data_length, dec_data.ptr()))) {
    return CreateErrorWrap<TPMError>(std::move(err),
                                     "Error calling Tspi_Data_Unseal");
  }
  plaintext->assign(&dec_data.value()[0], &dec_data.value()[dec_data_length]);
  brillo::SecureClearBytes(dec_data.value(), dec_data_length);

  return nullptr;
}

TPM1Error TpmImpl::GetPublicKeyBlob(TSS_HCONTEXT context_handle,
                                    TSS_HKEY key_handle,
                                    SecureBlob* data_out) const {
  ScopedTssMemory blob(context_handle);
  UINT32 blob_size;

  if (auto err = CreateError<TPM1Error>(
          Tspi_Key_GetPubKey(key_handle, &blob_size, blob.ptr()))) {
    LOG(ERROR) << "Error calling Tspi_Key_GetPubKey: " << *err;
    return err;
  }

  SecureBlob local_data(blob_size);
  memcpy(local_data.data(), blob.value(), blob_size);
  brillo::SecureClearBytes(blob.value(), blob_size);
  data_out->swap(local_data);
  return nullptr;
}

TPM1Error TpmImpl::LoadSrk(TSS_HCONTEXT context_handle, TSS_HKEY* srk_handle) {
  // We shouldn't load the SRK if the TPM have been fully owned.
  if (!IsOwned()) {
    return CreateError<TPM1Error>(TSS_LAYER_TCS | TSS_E_FAIL);
  }

  // Load the Storage Root Key
  TSS_UUID SRK_UUID = TSS_UUID_SRK;
  ScopedTssKey local_srk_handle(context_handle);
  if (auto err = CreateError<TPM1Error>(
          Tspi_Context_LoadKeyByUUID(context_handle, TSS_PS_TYPE_SYSTEM,
                                     SRK_UUID, local_srk_handle.ptr()))) {
    return err;
  }

  // Check if the SRK wants a password
  UINT32 srk_authusage;
  if (auto err = CreateError<TPM1Error>(Tspi_GetAttribUint32(
          local_srk_handle, TSS_TSPATTRIB_KEY_INFO,
          TSS_TSPATTRIB_KEYINFO_AUTHUSAGE, &srk_authusage))) {
    return err;
  }

  // Give it the password if needed
  if (srk_authusage) {
    TSS_HPOLICY srk_usage_policy;
    if (auto err = CreateError<TPM1Error>(Tspi_GetPolicyObject(
            local_srk_handle, TSS_POLICY_USAGE, &srk_usage_policy))) {
      return err;
    }

    if (auto err = CreateError<TPM1Error>(Tspi_Policy_SetSecret(
            srk_usage_policy, TSS_SECRET_MODE_PLAIN, srk_auth_.size(),
            const_cast<BYTE*>(srk_auth_.data())))) {
      return err;
    }
  }

  *srk_handle = local_srk_handle.release();
  return nullptr;
}

bool TpmImpl::CreateEndorsementKey() {
  TSS_HTPM tpm_handle;
  if (!GetTpm(tpm_context_.value(), &tpm_handle)) {
    return false;
  }

  ScopedTssKey local_key_handle(tpm_context_.value());
  TSS_FLAG init_flags = TSS_KEY_TYPE_LEGACY | TSS_KEY_SIZE_2048;
  if (auto err = CreateError<TPM1Error>(Tspi_Context_CreateObject(
          tpm_context_.value(), TSS_OBJECT_TYPE_RSAKEY, init_flags,
          local_key_handle.ptr()))) {
    LOG(ERROR) << "Error calling Tspi_Context_CreateObject: " << *err;
    return false;
  }

  if (auto err = CreateError<TPM1Error>(
          Tspi_TPM_CreateEndorsementKey(tpm_handle, local_key_handle, NULL))) {
    LOG(ERROR) << "Error calling Tspi_TPM_CreateEndorsementKey: " << *err;
    return false;
  }

  return true;
}

bool TpmImpl::IsEndorsementKeyAvailable() {
  TSS_HTPM tpm_handle;
  if (!GetTpm(tpm_context_.value(), &tpm_handle)) {
    return false;
  }

  ScopedTssKey local_key_handle(tpm_context_.value());
  if (auto err = CreateError<TPM1Error>(Tspi_TPM_GetPubEndorsementKey(
          tpm_handle, false, NULL, local_key_handle.ptr()))) {
    LOG(ERROR) << "Error calling Tspi_TPM_GetPubEndorsementKey: " << *err;
    return false;
  }

  return true;
}

bool TpmImpl::TakeOwnership(int, const SecureBlob&) {
  if (!InitializeTpmManagerUtility()) {
    LOG(ERROR) << __func__ << ": Failed to initialize |TpmManagerUtility|.";
    return false;
  }
  if (IsOwned()) {
    LOG(INFO) << __func__ << ": TPM is already owned.";
    return true;
  }
  return tpm_manager_utility_->TakeOwnership();
}

bool TpmImpl::GetTpm(TSS_HCONTEXT context_handle, TSS_HTPM* tpm_handle) {
  TSS_HTPM local_tpm_handle;
  if (auto err = CreateError<TPM1Error>(
          Tspi_Context_GetTpmObject(context_handle, &local_tpm_handle))) {
    LOG(ERROR) << "Error calling Tspi_Context_GetTpmObject: " << *err;
    return false;
  }
  *tpm_handle = local_tpm_handle;
  return true;
}

bool TpmImpl::GetTpmWithAuth(TSS_HCONTEXT context_handle,
                             const SecureBlob& owner_password,
                             TSS_HTPM* tpm_handle) {
  TSS_HTPM local_tpm_handle;
  if (!GetTpm(context_handle, &local_tpm_handle)) {
    return false;
  }

  TSS_HPOLICY tpm_usage_policy;
  if (auto err = CreateError<TPM1Error>(Tspi_GetPolicyObject(
          local_tpm_handle, TSS_POLICY_USAGE, &tpm_usage_policy))) {
    LOG(ERROR) << "Error calling Tspi_GetPolicyObject: " << *err;
    return false;
  }

  if (auto err = CreateError<TPM1Error>(Tspi_Policy_SetSecret(
          tpm_usage_policy, TSS_SECRET_MODE_PLAIN, owner_password.size(),
          const_cast<BYTE*>(owner_password.data())))) {
    LOG(ERROR) << "Error calling Tspi_Policy_SetSecret: " << *err;
    return false;
  }

  *tpm_handle = local_tpm_handle;
  return true;
}

bool TpmImpl::GetTpmWithDelegation(TSS_HCONTEXT context_handle,
                                   const brillo::Blob& delegate_blob,
                                   const brillo::Blob& delegate_secret,
                                   TSS_HTPM* tpm_handle) {
  TSS_HTPM local_tpm_handle;
  if (!GetTpm(context_handle, &local_tpm_handle)) {
    return false;
  }

  TSS_HPOLICY tpm_usage_policy;
  if (auto err = CreateError<TPM1Error>(Tspi_GetPolicyObject(
          local_tpm_handle, TSS_POLICY_USAGE, &tpm_usage_policy))) {
    LOG(ERROR) << "Error calling Tspi_GetPolicyObject: " << *err;
    return false;
  }

  BYTE* secret_buffer = const_cast<BYTE*>(delegate_secret.data());
  if (auto err = CreateError<TPM1Error>(
          Tspi_Policy_SetSecret(tpm_usage_policy, TSS_SECRET_MODE_PLAIN,
                                delegate_secret.size(), secret_buffer))) {
    LOG(ERROR) << "Error calling Tspi_Policy_SetSecret: " << *err;
    return false;
  }

  if (auto err = CreateError<TPM1Error>(Tspi_SetAttribData(
          tpm_usage_policy, TSS_TSPATTRIB_POLICY_DELEGATION_INFO,
          TSS_TSPATTRIB_POLDEL_OWNERBLOB, delegate_blob.size(),
          const_cast<BYTE*>(delegate_blob.data())))) {
    LOG(ERROR) << "Error calling Tspi_SetAttribData: " << *err;
    return false;
  }

  *tpm_handle = local_tpm_handle;
  return true;
}

bool TpmImpl::GetOwnerPassword(brillo::SecureBlob* owner_password) {
  if (IsOwned()) {
    *owner_password =
        brillo::SecureBlob(last_tpm_manager_data_.owner_password());
    if (owner_password->empty()) {
      LOG(WARNING) << __func__
                   << ": Trying to get owner password after it is cleared.";
    }
  } else {
    LOG(ERROR)
        << __func__
        << ": Cannot get owner password until TPM is confirmed to be owned.";
    owner_password->clear();
  }
  return !owner_password->empty();
}

bool TpmImpl::GetRandomDataBlob(size_t length, brillo::Blob* data) {
  brillo::SecureBlob blob(length);
  if (!this->GetRandomDataSecureBlob(length, &blob)) {
    LOG(ERROR) << "GetRandomDataBlob failed";
    return false;
  }
  data->assign(blob.begin(), blob.end());
  return true;
}

bool TpmImpl::GetRandomDataSecureBlob(size_t length, brillo::SecureBlob* data) {
  ScopedTssContext context_handle;
  if ((*(context_handle.ptr()) = ConnectContext()) == 0) {
    LOG(ERROR) << "Could not open the TPM";
    return false;
  }

  TSS_HTPM tpm_handle;
  if (!GetTpm(context_handle, &tpm_handle)) {
    LOG(ERROR) << "Could not get a handle to the TPM";
    return false;
  }

  SecureBlob random(length);
  ScopedTssMemory tpm_data(context_handle);
  if (auto err = CreateError<TPM1Error>(
          Tspi_TPM_GetRandom(tpm_handle, random.size(), tpm_data.ptr()))) {
    LOG(ERROR) << "Could not get random data from the TP1: " << *err;
    return false;
  }
  memcpy(random.data(), tpm_data.value(), random.size());
  brillo::SecureClearBytes(tpm_data.value(), random.size());
  data->swap(random);
  return true;
}

bool TpmImpl::GetAlertsData(Tpm::AlertsData* alerts) {
  return false;
}

bool TpmImpl::DestroyNvram(uint32_t index) {
  if (!InitializeTpmManagerUtility()) {
    LOG(ERROR) << __func__ << ": Failed to initialize |TpmManagerUtility|.";
    return false;
  }
  return tpm_manager_utility_->DestroySpace(index);
}

bool TpmImpl::DefineNvram(uint32_t index, size_t length, uint32_t flags) {
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

bool TpmImpl::IsNvramDefined(uint32_t index) {
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

unsigned int TpmImpl::GetNvramSize(uint32_t index) {
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

bool TpmImpl::IsNvramLocked(uint32_t index) {
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

bool TpmImpl::ReadNvram(uint32_t index, SecureBlob* blob) {
  if (!InitializeTpmManagerUtility()) {
    return false;
  }

  std::string output;
  const bool result = tpm_manager_utility_->ReadSpace(index, false, &output);
  brillo::SecureBlob tmp(output);
  blob->swap(tmp);
  return result;
}

bool TpmImpl::WriteNvram(uint32_t index, const SecureBlob& blob) {
  if (!InitializeTpmManagerUtility()) {
    LOG(ERROR) << __func__ << ": Failed to initialize |TpmManagerUtility|.";
    return false;
  }
  return tpm_manager_utility_->WriteSpace(index, blob.to_string(),
                                          /*use_owner_auth=*/false);
}

bool TpmImpl::OwnerWriteNvram(uint32_t index, const SecureBlob& blob) {
  // Not implemented in TPM 1.2.
  // Note that technically the implementation should be the same as
  // `Tpm2Impl::OwnerWriteNvram()`; however, because 1. there is no demand by
  // cryptohome and 2. there is no active consumption of OWNERWRITE case for
  // TPM1.2, it is unnecessary and confusing to implement this block.
  return false;
}

bool TpmImpl::WriteLockNvram(uint32_t index) {
  if (!InitializeTpmManagerUtility()) {
    LOG(ERROR) << __func__ << ": Failed to initialize |TpmManagerUtility|.";
    return false;
  }
  return tpm_manager_utility_->LockSpace(index);
}

bool TpmImpl::PerformEnabledOwnedCheck(bool* enabled, bool* owned) {
  *enabled = false;
  *owned = false;

  trousers::ScopedTssContext context(ConnectContext());
  if (!context) {
    return false;
  }

  TSS_HCONTEXT context_handle = context.context();
  TSS_HTPM tpm_handle;

  if (auto err = CreateError<TPM1Error>(
          Tspi_Context_GetTpmObject(context_handle, &tpm_handle))) {
    LOG(ERROR) << "Error calling Tspi_Context_GetTpmObject: " << *err;
    return false;
  }

  UINT32 sub_cap = TSS_TPMCAP_PROP_OWNER;
  UINT32 cap_length = 0;
  trousers::ScopedTssMemory cap(context_handle);
  if (auto err = CreateError<TPM1Error>(Tspi_TPM_GetCapability(
          tpm_handle, TSS_TPMCAP_PROPERTY, sizeof(sub_cap),
          reinterpret_cast<BYTE*>(&sub_cap), &cap_length, cap.ptr()))) {
    if (ERROR_CODE(err->ErrorCode()) == TPM_E_DISABLED) {
      *enabled = false;
    }
  } else if (cap_length >= (sizeof(TSS_BOOL))) {
    *enabled = true;
    *owned = ((*(reinterpret_cast<TSS_BOOL*>(cap.value()))) != 0);
  }

  return true;
}

bool TpmImpl::SealToPCR0(const brillo::SecureBlob& value,
                         brillo::SecureBlob* sealed_value) {
  CHECK(sealed_value);
  ScopedTssContext context_handle;
  TSS_HTPM tpm_handle;
  if (!ConnectContextAsUser(context_handle.ptr(), &tpm_handle)) {
    LOG(ERROR) << "SealToPCR0: Failed to connect to the TPM.";
    return false;
  }
  // Load the Storage Root Key.
  ScopedTssKey srk_handle(context_handle);
  if (TPM1Error err = LoadSrk(context_handle, srk_handle.ptr())) {
    LOG(ERROR) << __func__ << ": Failed to load SRK: " << *err;
    return false;
  }

  // Check the SRK public key
  unsigned int size_n = 0;
  ScopedTssMemory public_srk(context_handle);
  if (auto err = CreateError<TPM1Error>(
          Tspi_Key_GetPubKey(srk_handle, &size_n, public_srk.ptr()))) {
    LOG(ERROR) << __func__ << ": Unable to get the SRK public key: " << *err;
    return false;
  }

  // Create a PCRS object which holds the value of PCR0.
  ScopedTssPcrs pcrs_handle(context_handle);
  if (auto err = CreateError<TPM1Error>(
          Tspi_Context_CreateObject(context_handle, TSS_OBJECT_TYPE_PCRS,
                                    TSS_PCRS_STRUCT_INFO, pcrs_handle.ptr()))) {
    LOG(ERROR) << __func__
               << ": Error calling Tspi_Context_CreateObject: " << *err;
    return false;
  }

  // Create a ENCDATA object to receive the sealed data.
  UINT32 pcr_len = 0;
  ScopedTssMemory pcr_value(context_handle);
  Tspi_TPM_PcrRead(tpm_handle, 0, &pcr_len, pcr_value.ptr());
  Tspi_PcrComposite_SetPcrValue(pcrs_handle, 0, pcr_len, pcr_value.value());

  ScopedTssKey enc_handle(context_handle);
  if (auto err = CreateError<TPM1Error>(
          Tspi_Context_CreateObject(context_handle, TSS_OBJECT_TYPE_ENCDATA,
                                    TSS_ENCDATA_SEAL, enc_handle.ptr()))) {
    LOG(ERROR) << __func__
               << ": Error calling Tspi_Context_CreateObject: " << *err;
    return false;
  }

  // Seal the given value with the SRK.
  if (auto err = CreateError<TPM1Error>(
          Tspi_Data_Seal(enc_handle, srk_handle, value.size(),
                         const_cast<BYTE*>(value.data()), pcrs_handle))) {
    LOG(ERROR) << __func__ << ": Error calling Tspi_Data_Seal: " << *err;
    return false;
  }

  // Extract the sealed value.
  ScopedTssMemory enc_data(context_handle);
  UINT32 enc_data_length = 0;
  if (auto err = CreateError<TPM1Error>(Tspi_GetAttribData(
          enc_handle, TSS_TSPATTRIB_ENCDATA_BLOB,
          TSS_TSPATTRIB_ENCDATABLOB_BLOB, &enc_data_length, enc_data.ptr()))) {
    LOG(ERROR) << __func__ << ": Error calling Tspi_GetAttribData: " << *err;
    return false;
  }
  sealed_value->assign(&enc_data.value()[0],
                       &enc_data.value()[enc_data_length]);
  return true;
}

bool TpmImpl::Unseal(const brillo::SecureBlob& sealed_value,
                     brillo::SecureBlob* value) {
  CHECK(value);
  ScopedTssContext context_handle;
  TSS_HTPM tpm_handle;
  if (!ConnectContextAsUser(context_handle.ptr(), &tpm_handle)) {
    LOG(ERROR) << "Unseal: Failed to connect to the TPM.";
    return false;
  }
  // Load the Storage Root Key.
  ScopedTssKey srk_handle(context_handle);
  if (TPM1Error err = LoadSrk(context_handle, srk_handle.ptr())) {
    LOG(ERROR) << __func__ << ": Failed to load SRK: " << *err;
    return false;
  }

  // Create an ENCDATA object with the sealed value.
  ScopedTssKey enc_handle(context_handle);
  if (auto err = CreateError<TPM1Error>(
          Tspi_Context_CreateObject(context_handle, TSS_OBJECT_TYPE_ENCDATA,
                                    TSS_ENCDATA_SEAL, enc_handle.ptr()))) {
    LOG(ERROR) << __func__
               << ": Error calling Tspi_Context_CreateObject: " << *err;
    return false;
  }

  if (auto err = CreateError<TPM1Error>(Tspi_SetAttribData(
          enc_handle, TSS_TSPATTRIB_ENCDATA_BLOB,
          TSS_TSPATTRIB_ENCDATABLOB_BLOB, sealed_value.size(),
          const_cast<BYTE*>(sealed_value.data())))) {
    LOG(ERROR) << __func__ << ": Error calling Tspi_SetAttribData: " << *err;
    return false;
  }

  // Unseal using the SRK.
  ScopedTssMemory dec_data(context_handle);
  UINT32 dec_data_length = 0;
  if (auto err = CreateError<TPM1Error>(Tspi_Data_Unseal(
          enc_handle, srk_handle, &dec_data_length, dec_data.ptr()))) {
    LOG(ERROR) << __func__ << ": Error calling Tspi_Data_Unseal: " << *err;
    return false;
  }
  value->assign(&dec_data.value()[0], &dec_data.value()[dec_data_length]);
  brillo::SecureClearBytes(dec_data.value(), dec_data_length);
  return true;
}

bool TpmImpl::CreateDelegate(const std::set<uint32_t>& bound_pcrs,
                             uint8_t delegate_family_label,
                             uint8_t delegate_label,
                             Blob* delegate_blob,
                             Blob* delegate_secret) {
  CHECK(delegate_blob && delegate_secret);

  // Connect to the TPM as the owner.
  ScopedTssContext context_handle;
  TSS_HTPM tpm_handle;
  if (!ConnectContextAsOwner(context_handle.ptr(), &tpm_handle)) {
    LOG(ERROR) << __func__ << ": Could not connect to the TPM.";
    return false;
  }

  // Generate a delegate secret.
  if (!GetRandomDataBlob(kDelegateSecretSize, delegate_secret)) {
    return false;
  }

  // Create an owner delegation policy.
  ScopedTssPolicy policy(context_handle);
  if (auto err = CreateError<TPM1Error>(
          Tspi_Context_CreateObject(context_handle, TSS_OBJECT_TYPE_POLICY,
                                    TSS_POLICY_USAGE, policy.ptr()))) {
    LOG(ERROR) << __func__ << ": Failed to create policy: " << *err;
    return false;
  }
  if (auto err = CreateError<TPM1Error>(Tspi_Policy_SetSecret(
          policy, TSS_SECRET_MODE_PLAIN, delegate_secret->size(),
          delegate_secret->data()))) {
    LOG(ERROR) << __func__ << ": Failed to set policy secret: " << *err;
    return false;
  }
  if (auto err = CreateError<TPM1Error>(Tspi_SetAttribUint32(
          policy, TSS_TSPATTRIB_POLICY_DELEGATION_INFO,
          TSS_TSPATTRIB_POLDEL_TYPE, TSS_DELEGATIONTYPE_OWNER))) {
    LOG(ERROR) << __func__ << ": Failed to set delegation type: " << *err;
    return false;
  }
  // These are the privileged operations we will allow the delegate to perform.
  const UINT32 permissions =
      TPM_DELEGATE_ActivateIdentity | TPM_DELEGATE_DAA_Join |
      TPM_DELEGATE_DAA_Sign | TPM_DELEGATE_ResetLockValue |
      TPM_DELEGATE_OwnerReadInternalPub | TPM_DELEGATE_CMK_ApproveMA |
      TPM_DELEGATE_CMK_CreateTicket | TPM_DELEGATE_AuthorizeMigrationKey;
  if (auto err = CreateError<TPM1Error>(
          Tspi_SetAttribUint32(policy, TSS_TSPATTRIB_POLICY_DELEGATION_INFO,
                               TSS_TSPATTRIB_POLDEL_PER1, permissions))) {
    LOG(ERROR) << __func__ << ": Failed to set permissions: " << *err;
    return false;
  }
  if (auto err = CreateError<TPM1Error>(
          Tspi_SetAttribUint32(policy, TSS_TSPATTRIB_POLICY_DELEGATION_INFO,
                               TSS_TSPATTRIB_POLDEL_PER2, 0))) {
    LOG(ERROR) << __func__ << ": Failed to set permissions: " << *err;
    return false;
  }

  // Bind the delegate to the specified PCRs. Note: it's crucial to pass a null
  // TSS_HPCRS to Tspi_TPM_Delegate_CreateDelegation() when no PCR is selected,
  // otherwise it will fail with TPM_E_BAD_PARAM_SIZE.
  ScopedTssPcrs pcrs_handle(context_handle);
  if (!bound_pcrs.empty()) {
    if (auto err = CreateError<TPM1Error>(Tspi_Context_CreateObject(
            context_handle, TSS_OBJECT_TYPE_PCRS, TSS_PCRS_STRUCT_INFO_SHORT,
            pcrs_handle.ptr()))) {
      LOG(ERROR) << __func__ << ": Failed to create PCRS object: " << *err;
      return false;
    }
    for (auto bound_pcr : bound_pcrs) {
      UINT32 pcr_len = 0;
      ScopedTssMemory pcr_value(context_handle);
      if (auto err = CreateError<TPM1Error>(Tspi_TPM_PcrRead(
              tpm_handle, bound_pcr, &pcr_len, pcr_value.ptr()))) {
        LOG(ERROR) << __func__ << ": Could not read PCR value: " << *err;
        return false;
      }
      if (auto err = CreateError<TPM1Error>(Tspi_PcrComposite_SetPcrValue(
              pcrs_handle, bound_pcr, pcr_len, pcr_value.value()))) {
        LOG(ERROR) << __func__
                   << ": Could not set value for PCR in PCRS handle: " << *err;
        return false;
      }
    }
    if (auto err = CreateError<TPM1Error>(
            Tspi_PcrComposite_SetPcrLocality(pcrs_handle, kTpmPCRLocality))) {
      LOG(ERROR) << __func__
                 << ": Could not set locality for PCRs in PCRS handle: "
                 << *err;
      return false;
    }
  }

  // Create a delegation family.
  ScopedTssObject<TSS_HDELFAMILY> family(context_handle);
  if (auto err = CreateError<TPM1Error>(Tspi_TPM_Delegate_AddFamily(
          tpm_handle, delegate_family_label, family.ptr()))) {
    LOG(ERROR) << __func__ << ": Failed to create family: " << *err;
    return false;
  }

  // Create the delegation.
  if (auto err = CreateError<TPM1Error>(Tspi_TPM_Delegate_CreateDelegation(
          tpm_handle, delegate_label, 0, pcrs_handle, family, policy))) {
    LOG(ERROR) << __func__ << ": Failed to create delegation: " << *err;
    return false;
  }

  // Enable the delegation family.
  if (auto err = CreateError<TPM1Error>(
          Tspi_SetAttribUint32(family, TSS_TSPATTRIB_DELFAMILY_STATE,
                               TSS_TSPATTRIB_DELFAMILYSTATE_ENABLED, TRUE))) {
    LOG(ERROR) << __func__ << ": Failed to enable family: " << *err;
    return false;
  }

  // Save the delegation blob for later.
  SecureBlob delegate;
  if (TPM1Error err = GetDataAttribute(
          context_handle, policy, TSS_TSPATTRIB_POLICY_DELEGATION_INFO,
          TSS_TSPATTRIB_POLDEL_OWNERBLOB, &delegate)) {
    LOG(ERROR) << __func__ << ": Failed to get delegate blob: " << *err;
    return false;
  }
  delegate_blob->assign(delegate.begin(), delegate.end());
  is_delegate_bound_to_pcr_ = !bound_pcrs.empty();
  has_reset_lock_permissions_ = true;

  return true;
}

bool TpmImpl::Sign(const SecureBlob& key_blob,
                   const SecureBlob& input,
                   uint32_t bound_pcr_index,
                   SecureBlob* signature) {
  CHECK(signature);
  ScopedTssContext context_handle;
  TSS_HTPM tpm_handle;
  if (!ConnectContextAsUser(context_handle.ptr(), &tpm_handle)) {
    LOG(ERROR) << __func__ << ": Failed to connect to the TPM.";
    return false;
  }

  // Load the Storage Root Key.
  ScopedTssKey srk_handle(context_handle);
  if (TPM1Error err = LoadSrk(context_handle, srk_handle.ptr())) {
    LOG(ERROR) << __func__ << ": Failed to load SRK: " << *err;
    return false;
  }

  // Load the key (which should be wrapped by the SRK).
  ScopedTssKey key_handle(context_handle);
  if (auto err = CreateError<TPM1Error>(Tspi_Context_LoadKeyByBlob(
          context_handle, srk_handle, key_blob.size(),
          const_cast<BYTE*>(key_blob.data()), key_handle.ptr()))) {
    LOG(ERROR) << __func__ << ": Failed to load key: " << *err;
    return false;
  }

  // Create a hash object to hold the input.
  ScopedTssObject<TSS_HHASH> hash_handle(context_handle);
  if (auto err = CreateError<TPM1Error>(
          Tspi_Context_CreateObject(context_handle, TSS_OBJECT_TYPE_HASH,
                                    TSS_HASH_OTHER, hash_handle.ptr()))) {
    LOG(ERROR) << __func__ << ": Failed to create hash object: " << *err;
    return false;
  }

  // Create the DER encoded input.
  SecureBlob der_header(std::begin(kSha256DigestInfo),
                        std::end(kSha256DigestInfo));
  SecureBlob der_encoded_input = SecureBlob::Combine(der_header, Sha256(input));

  // Don't hash anything, just push the input data into the hash object.
  if (auto err = CreateError<TPM1Error>(Tspi_Hash_SetHashValue(
          hash_handle, der_encoded_input.size(),
          const_cast<BYTE*>(der_encoded_input.data())))) {
    LOG(ERROR) << __func__ << ": Failed to set hash data: " << *err;
    return false;
  }

  UINT32 length = 0;
  ScopedTssMemory buffer(context_handle);
  if (auto err = CreateError<TPM1Error>(
          Tspi_Hash_Sign(hash_handle, key_handle, &length, buffer.ptr()))) {
    LOG(ERROR) << __func__ << ": Failed to generate signature: " << *err;
    return false;
  }
  SecureBlob tmp(buffer.value(), buffer.value() + length);
  brillo::SecureClearBytes(buffer.value(), length);
  signature->swap(tmp);
  return true;
}

bool TpmImpl::CreatePCRBoundKey(const std::map<uint32_t, std::string>& pcr_map,
                                AsymmetricKeyUsage key_type,
                                brillo::SecureBlob* key_blob,
                                brillo::SecureBlob* public_key_der,
                                brillo::SecureBlob* creation_blob) {
  CHECK(creation_blob) << "Error no creation_blob.";
  creation_blob->clear();
  ScopedTssContext context_handle;
  TSS_HTPM tpm_handle;
  if (!ConnectContextAsUser(context_handle.ptr(), &tpm_handle)) {
    LOG(ERROR) << __func__ << ": Failed to connect to the TPM.";
    return false;
  }

  // Load the Storage Root Key.
  ScopedTssKey srk_handle(context_handle);
  if (TPM1Error err = LoadSrk(context_handle, srk_handle.ptr())) {
    LOG(ERROR) << __func__ << ": Failed to load SRK: " << *err;
    return false;
  }

  // Create a PCRS object to hold pcr_index and pcr_value.
  ScopedTssPcrs pcrs(context_handle);
  if (auto err = CreateError<TPM1Error>(
          Tspi_Context_CreateObject(context_handle, TSS_OBJECT_TYPE_PCRS,
                                    TSS_PCRS_STRUCT_INFO, pcrs.ptr()))) {
    LOG(ERROR) << __func__ << ": Failed to create PCRS object: " << *err;
    return false;
  }

  for (const auto& map_pair : pcr_map) {
    uint32_t pcr_index = map_pair.first;
    Blob pcr_value(BlobFromString(map_pair.second));
    if (pcr_value.empty()) {
      if (!ReadPCR(pcr_index, &pcr_value)) {
        LOG(ERROR) << __func__ << ": Failed to read PCR.";
        return false;
      }
    }

    BYTE* pcr_value_buffer = const_cast<BYTE*>(pcr_value.data());
    Tspi_PcrComposite_SetPcrValue(pcrs, pcr_index, pcr_value.size(),
                                  pcr_value_buffer);
  }

  // Create a non-migratable key restricted to |pcrs|.
  ScopedTssKey pcr_bound_key(context_handle);
  TSS_FLAG init_flags =
      TSS_KEY_VOLATILE | TSS_KEY_NOT_MIGRATABLE | kDefaultTpmRsaKeyFlag;
  switch (key_type) {
    case AsymmetricKeyUsage::kDecryptKey:
      // In this case, the key is not decrypt only. It can be used to sign the
      // data too. No easy way to make a decrypt only key here.
      init_flags |= TSS_KEY_TYPE_LEGACY;
      break;
    case AsymmetricKeyUsage::kSignKey:
      init_flags |= TSS_KEY_TYPE_SIGNING;
      break;
    case AsymmetricKeyUsage::kDecryptAndSignKey:
      init_flags |= TSS_KEY_TYPE_LEGACY;
      break;
  }
  if (auto err = CreateError<TPM1Error>(
          Tspi_Context_CreateObject(context_handle, TSS_OBJECT_TYPE_RSAKEY,
                                    init_flags, pcr_bound_key.ptr()))) {
    LOG(ERROR) << __func__ << ": Failed to create object: " << *err;
    return false;
  }

  if (auto err = CreateError<TPM1Error>(Tspi_SetAttribUint32(
          pcr_bound_key, TSS_TSPATTRIB_KEY_INFO,
          TSS_TSPATTRIB_KEYINFO_SIGSCHEME, TSS_SS_RSASSAPKCS1V15_DER))) {
    LOG(ERROR) << __func__ << ": Failed to set signature scheme: " << *err;
    return false;
  }
  if (auto err = CreateError<TPM1Error>(
          Tspi_Key_CreateKey(pcr_bound_key, srk_handle, pcrs))) {
    LOG(ERROR) << __func__ << ": Failed to create key: " << *err;
    return false;
  }
  if (auto err =
          CreateError<TPM1Error>(Tspi_Key_LoadKey(pcr_bound_key, srk_handle))) {
    LOG(ERROR) << __func__ << ": Failed to load key: " << *err;
    return false;
  }

  // Get the public key.
  SecureBlob public_key;
  if (TPM1Error err = GetDataAttribute(
          context_handle, pcr_bound_key, TSS_TSPATTRIB_KEY_BLOB,
          TSS_TSPATTRIB_KEYBLOB_PUBLIC_KEY, &public_key)) {
    LOG(ERROR) << __func__ << ": Failed to read public key: " << *err;
    return false;
  }
  if (!ConvertPublicKeyToDER(public_key, public_key_der)) {
    return false;
  }

  // Get the key blob so we can load it later.
  if (TPM1Error err = GetDataAttribute(context_handle, pcr_bound_key,
                                       TSS_TSPATTRIB_KEY_BLOB,
                                       TSS_TSPATTRIB_KEYBLOB_BLOB, key_blob)) {
    LOG(ERROR) << __func__ << ": Failed to read key blob: " << *err;
    return false;
  }
  return true;
}

bool TpmImpl::VerifyPCRBoundKey(const std::map<uint32_t, std::string>& pcr_map,
                                const brillo::SecureBlob& key_blob,
                                const brillo::SecureBlob& creation_blob) {
  ScopedTssContext context_handle;
  TSS_HTPM tpm_handle;
  if (!ConnectContextAsUser(context_handle.ptr(), &tpm_handle)) {
    LOG(ERROR) << __func__ << ": Failed to connect to the TPM.";
    return false;
  }

  ScopedTssKey srk_handle(context_handle);
  if (TPM1Error err = LoadSrk(context_handle, srk_handle.ptr())) {
    LOG(ERROR) << __func__ << ": Failed to load SRK: " << *err;
    return false;
  }

  ScopedTssKey key(context_handle);
  if (auto err = CreateError<TPM1Error>(Tspi_Context_LoadKeyByBlob(
          context_handle, srk_handle, key_blob.size(),
          const_cast<BYTE*>(key_blob.data()), key.ptr()))) {
    LOG(ERROR) << __func__ << ": Failed to load key: " << *err;
    return false;
  }

  // Check that |pcr_index| is selected.
  SecureBlob pcr_selection_blob;
  if (TPM1Error err = GetDataAttribute(
          context_handle, key, TSS_TSPATTRIB_KEY_PCR,
          TSS_TSPATTRIB_KEYPCR_SELECTION, &pcr_selection_blob)) {
    LOG(ERROR) << __func__
               << ": Failed to read PCR selection for key: " << *err;
    return false;
  }
  UINT64 trspi_offset = 0;
  TPM_PCR_SELECTION pcr_selection;
  Trspi_UnloadBlob_PCR_SELECTION(&trspi_offset, pcr_selection_blob.data(),
                                 &pcr_selection);
  if (!pcr_selection.pcrSelect) {
    LOG(ERROR) << __func__ << ": No PCR selected.";
    return false;
  }
  const Blob pcr_bitmap(pcr_selection.pcrSelect,
                        pcr_selection.pcrSelect + pcr_selection.sizeOfSelect);
  free(pcr_selection.pcrSelect);
  std::string concatenated_pcr_values;
  for (const auto& map_pair : pcr_map) {
    uint32_t pcr_index = map_pair.first;
    const std::string pcr_value = map_pair.second;
    size_t offset = pcr_index / 8;
    unsigned char mask = 1 << (pcr_index % 8);
    if (pcr_bitmap.size() <= offset || (pcr_bitmap[offset] & mask) == 0) {
      LOG(ERROR) << __func__ << ": Invalid PCR selection.";
      return false;
    }

    concatenated_pcr_values += pcr_value;
  }

  // Compute the PCR composite hash we're expecting. Basically, we want to do
  // the equivalent of hashing a TPM_PCR_COMPOSITE structure.
  trspi_offset = 0;
  UINT32 pcr_value_length = concatenated_pcr_values.size();
  Blob pcr_value_length_blob(sizeof(UINT32));
  Trspi_LoadBlob_UINT32(&trspi_offset, pcr_value_length,
                        pcr_value_length_blob.data());
  const SecureBlob pcr_hash = Sha1ToSecureBlob(CombineBlobs(
      {Blob(pcr_selection_blob.begin(), pcr_selection_blob.end()),
       pcr_value_length_blob, BlobFromString(concatenated_pcr_values)}));

  // Check that the PCR value matches the key creation PCR value.
  SecureBlob pcr_at_creation;
  if (TPM1Error err = GetDataAttribute(
          context_handle, key, TSS_TSPATTRIB_KEY_PCR,
          TSS_TSPATTRIB_KEYPCR_DIGEST_ATCREATION, &pcr_at_creation)) {
    LOG(ERROR) << __func__ << ": Failed to read PCR value at key creation"
               << *err;
    return false;
  }

  if (pcr_at_creation != pcr_hash) {
    LOG(ERROR) << __func__ << ": Invalid key creation PCR.";
    return false;
  }

  // Check that the PCR value matches the PCR value required to use the key.
  SecureBlob pcr_at_release;
  if (TPM1Error err = GetDataAttribute(
          context_handle, key, TSS_TSPATTRIB_KEY_PCR,
          TSS_TSPATTRIB_KEYPCR_DIGEST_ATRELEASE, &pcr_at_release)) {
    LOG(ERROR) << __func__
               << ": Failed to read PCR value for key usage: " << *err;
    return false;
  }
  if (pcr_at_release != pcr_hash) {
    LOG(ERROR) << __func__ << ": Invalid key usage PCR.";
    return false;
  }
  return true;
}

bool TpmImpl::ExtendPCR(uint32_t pcr_index, const brillo::Blob& extension) {
  ScopedTssContext context_handle;
  TSS_HTPM tpm_handle;
  if (!ConnectContextAsUser(context_handle.ptr(), &tpm_handle)) {
    LOG(ERROR) << __func__ << ": Failed to connect to the TPM.";
    return false;
  }
  CHECK_EQ(extension.size(), kPCRExtensionSize);
  Blob mutable_extension = extension;
  UINT32 new_pcr_value_length = 0;
  ScopedTssMemory new_pcr_value(context_handle);
  if (auto err = CreateError<TPM1Error>(Tspi_TPM_PcrExtend(
          tpm_handle, pcr_index, extension.size(), mutable_extension.data(),
          NULL, &new_pcr_value_length, new_pcr_value.ptr()))) {
    LOG(ERROR) << "Failed to extend PCR " << pcr_index << ": " << *err;
    return false;
  }
  return true;
}

bool TpmImpl::ReadPCR(uint32_t pcr_index, brillo::Blob* pcr_value) {
  ScopedTssContext context_handle;
  TSS_HTPM tpm_handle;
  if (!ConnectContextAsUser(context_handle.ptr(), &tpm_handle)) {
    LOG(ERROR) << __func__ << ": Failed to connect to the TPM.";
    return false;
  }
  UINT32 pcr_len = 0;
  ScopedTssMemory pcr_value_buffer(context_handle);
  if (auto err = CreateError<TPM1Error>(Tspi_TPM_PcrRead(
          tpm_handle, pcr_index, &pcr_len, pcr_value_buffer.ptr()))) {
    LOG(ERROR) << "Could not read PCR " << pcr_index << ": " << *err;
    return false;
  }
  pcr_value->assign(pcr_value_buffer.value(),
                    pcr_value_buffer.value() + pcr_len);
  return true;
}

TPM1Error TpmImpl::GetDataAttribute(TSS_HCONTEXT context,
                                    TSS_HOBJECT object,
                                    TSS_FLAG flag,
                                    TSS_FLAG sub_flag,
                                    SecureBlob* data) const {
  UINT32 length = 0;
  ScopedTssMemory buf(context);
  if (auto err = CreateError<TPM1Error>(
          Tspi_GetAttribData(object, flag, sub_flag, &length, buf.ptr()))) {
    LOG(ERROR) << "Failed to read object attribute: " << *err;
    return err;
  }
  SecureBlob tmp(buf.value(), buf.value() + length);
  brillo::SecureClearBytes(buf.value(), length);
  data->swap(tmp);
  return nullptr;
}

bool TpmImpl::IsEnabled() {
  if (!is_enabled_) {
    if (!CacheTpmManagerStatus()) {
      LOG(ERROR) << __func__
                 << ": Failed to update TPM status from tpm manager.";
      return false;
    }
  }
  return is_enabled_;
}

bool TpmImpl::IsOwned() {
  if (!is_owned_) {
    if (!UpdateLocalDataFromTpmManager()) {
      LOG(ERROR) << __func__
                 << ": Failed to call |UpdateLocalDataFromTpmManager|.";
      return false;
    }
  }
  return is_owned_;
}

bool TpmImpl::IsOwnerPasswordPresent() {
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

bool TpmImpl::HasResetLockPermissions() {
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

bool TpmImpl::WrapRsaKey(const SecureBlob& public_modulus,
                         const SecureBlob& prime_factor,
                         SecureBlob* wrapped_key) {
  // Load the Storage Root Key
  trousers::ScopedTssKey srk_handle(tpm_context_.value());
  if (TPM1Error err = LoadSrk(tpm_context_.value(), srk_handle.ptr())) {
    if (err->ErrorCode() != kKeyNotFoundError) {
      LOG(ERROR) << __func__ << ": Failed to load SRK: " << *err;
    }
    return false;
  }

  // Make sure we can get the public key for the SRK.  If not, then the TPM
  // is not available.
  unsigned int size_n;
  trousers::ScopedTssMemory public_srk(tpm_context_.value());
  if (auto err = CreateError<TPM1Error>(
          Tspi_Key_GetPubKey(srk_handle, &size_n, public_srk.ptr()))) {
    LOG(ERROR) << __func__ << ": Cannot load SRK pub key: " << *err;
    return false;
  }

  // Create the key object
  TSS_FLAG init_flags = TSS_KEY_TYPE_LEGACY | TSS_KEY_VOLATILE |
                        TSS_KEY_MIGRATABLE | kDefaultTpmRsaKeyFlag;
  trousers::ScopedTssKey local_key_handle(tpm_context_.value());
  if (auto err = CreateError<TPM1Error>(Tspi_Context_CreateObject(
          tpm_context_.value(), TSS_OBJECT_TYPE_RSAKEY, init_flags,
          local_key_handle.ptr()))) {
    LOG(ERROR) << __func__
               << ": Error calling Tspi_Context_CreateObject: " << *err;
    return false;
  }

  // Set the attributes
  UINT32 sig_scheme = TSS_SS_RSASSAPKCS1V15_DER;
  if (auto err = CreateError<TPM1Error>(
          Tspi_SetAttribUint32(local_key_handle, TSS_TSPATTRIB_KEY_INFO,
                               TSS_TSPATTRIB_KEYINFO_SIGSCHEME, sig_scheme))) {
    LOG(ERROR) << __func__ << ": Error calling Tspi_SetAttribUint32: " << *err;
    return false;
  }

  UINT32 enc_scheme = TSS_ES_RSAESPKCSV15;
  if (auto err = CreateError<TPM1Error>(
          Tspi_SetAttribUint32(local_key_handle, TSS_TSPATTRIB_KEY_INFO,
                               TSS_TSPATTRIB_KEYINFO_ENCSCHEME, enc_scheme))) {
    LOG(ERROR) << __func__ << ": Error calling Tspi_SetAttribUint32: " << *err;
    return false;
  }

  // Set a random migration policy password, and discard it.  The key will not
  // be migrated, but to create the key outside of the TPM, we have to do it
  // this way.
  trousers::ScopedTssPolicy policy_handle(tpm_context_);
  if (!CreatePolicyWithRandomPassword(tpm_context_, TSS_POLICY_MIGRATION,
                                      policy_handle.ptr())) {
    LOG(ERROR) << __func__ << ": Error creating policy object";
    return false;
  }
  if (auto err = CreateError<TPM1Error>(
          Tspi_Policy_AssignToObject(policy_handle, local_key_handle))) {
    LOG(ERROR) << __func__ << ": Error assigning migration policy: " << *err;
    return false;
  }

  SecureBlob mutable_modulus(public_modulus.begin(), public_modulus.end());
  BYTE* public_modulus_buffer = static_cast<BYTE*>(mutable_modulus.data());
  if (auto err = CreateError<TPM1Error>(
          Tspi_SetAttribData(local_key_handle, TSS_TSPATTRIB_RSAKEY_INFO,
                             TSS_TSPATTRIB_KEYINFO_RSA_MODULUS,
                             public_modulus.size(), public_modulus_buffer))) {
    LOG(ERROR) << __func__ << ": Error setting RSA modulus: " << *err;
    return false;
  }
  SecureBlob mutable_factor(prime_factor.begin(), prime_factor.end());
  BYTE* prime_factor_buffer = static_cast<BYTE*>(mutable_factor.data());
  if (auto err = CreateError<TPM1Error>(
          Tspi_SetAttribData(local_key_handle, TSS_TSPATTRIB_KEY_BLOB,
                             TSS_TSPATTRIB_KEYBLOB_PRIVATE_KEY,
                             prime_factor.size(), prime_factor_buffer))) {
    LOG(ERROR) << __func__ << ": Error setting private key: " << *err;
    return false;
  }

  if (auto err = CreateError<TPM1Error>(
          Tspi_Key_WrapKey(local_key_handle, srk_handle, 0))) {
    LOG(ERROR) << __func__ << ": Error wrapping RSA key: " << *err;
    return false;
  }

  if (TPM1Error err =
          GetKeyBlob(tpm_context_.value(), local_key_handle, wrapped_key)) {
    LOG(ERROR) << "Failed to GetKeyBlob: " << *err;
    return false;
  }

  return true;
}

TPM1Error TpmImpl::GetKeyBlob(TSS_HCONTEXT context_handle,
                              TSS_HKEY key_handle,
                              SecureBlob* data_out) const {
  if (TPM1Error err =
          GetDataAttribute(context_handle, key_handle, TSS_TSPATTRIB_KEY_BLOB,
                           TSS_TSPATTRIB_KEYBLOB_BLOB, data_out)) {
    LOG(ERROR) << __func__ << ": Failed to get key blob " << *err;
    return err;
  }

  return nullptr;
}

TPMErrorBase TpmImpl::LoadWrappedKey(const brillo::SecureBlob& wrapped_key,
                                     ScopedKeyHandle* key_handle) {
  CHECK(key_handle);
  // Load the Storage Root Key
  trousers::ScopedTssKey srk_handle(tpm_context_.value());
  if (TPM1Error err = LoadSrk(tpm_context_.value(), srk_handle.ptr())) {
    if (err->ErrorCode() != kKeyNotFoundError) {
      ReportCryptohomeError(kCannotLoadTpmSrk);
    }
    return CreateErrorWrap<TPMError>(std::move(err), "Failed to load SRK");
  }

  // Make sure we can get the public key for the SRK.  If not, then the TPM
  // is not available.
  {
    SecureBlob pubkey;
    if (TPM1Error err =
            GetPublicKeyBlob(tpm_context_.value(), srk_handle, &pubkey)) {
      ReportCryptohomeError(kCannotReadTpmSrkPublic);
      return CreateErrorWrap<TPMError>(std::move(err),
                                       "Cannot load SRK public key");
    }
  }
  TpmKeyHandle local_key_handle;
  if (auto err = CreateError<TPM1Error>(Tspi_Context_LoadKeyByBlob(
          tpm_context_.value(), srk_handle, wrapped_key.size(),
          const_cast<BYTE*>(wrapped_key.data()), &local_key_handle))) {
    ReportCryptohomeError(kCannotLoadTpmKey);
    if (err->ErrorCode() == TPM_E_BAD_KEY_PROPERTY) {
      ReportCryptohomeError(kTpmBadKeyProperty);
    }
    return CreateErrorWrap<TPMError>(std::move(err),
                                     "Cannot load key from blob");
  }

  SecureBlob pub_key;
  // Make sure that we can get the public key
  if (TPM1Error err =
          GetPublicKeyBlob(tpm_context_.value(), local_key_handle, &pub_key)) {
    ReportCryptohomeError(kCannotReadTpmPublicKey);
    Tspi_Context_CloseObject(tpm_context_.value(), local_key_handle);
    return CreateErrorWrap<TPMError>(std::move(err),
                                     "Cannot get public key from blob");
  }
  key_handle->reset(this, local_key_handle);
  return nullptr;
}

bool TpmImpl::LegacyLoadCryptohomeKey(ScopedKeyHandle* key_handle,
                                      brillo::SecureBlob* key_blob) {
  CHECK(key_handle);
  TpmKeyHandle local_key_handle;
  if (auto err = CreateError<TPM1Error>(Tspi_Context_LoadKeyByUUID(
          tpm_context_.value(), TSS_PS_TYPE_SYSTEM, kCryptohomeWellKnownUuid,
          &local_key_handle))) {
    LOG(ERROR) << __func__ << ": failed LoadKeyByUUID: " << *err;
    return false;
  }

  if (key_blob) {
    if (TPM1Error err =
            GetKeyBlob(tpm_context_.value(), local_key_handle, key_blob)) {
      LOG(ERROR) << __func__ << ": failed to GetKeyBlob: " << *err;
      Tspi_Context_CloseObject(tpm_context_.value(), local_key_handle);
      return false;
    }
  }
  key_handle->reset(this, local_key_handle);
  return true;
}

void TpmImpl::CloseHandle(TpmKeyHandle key_handle) {
  Tspi_Context_CloseObject(tpm_context_.value(), key_handle);
}

bool TpmImpl::RemoveOwnerDependency(Tpm::TpmOwnerDependency dependency) {
  if (!InitializeTpmManagerUtility()) {
    LOG(ERROR) << __func__ << ": failed to initialize |TpmManagerUtility|.";
    return false;
  }
  return tpm_manager_utility_->RemoveOwnerDependency(
      OwnerDependencyEnumClassToString(dependency));
}

bool TpmImpl::ClearStoredPassword() {
  if (!InitializeTpmManagerUtility()) {
    LOG(ERROR) << __func__ << ": failed to initialize |TpmManagerUtility|.";
    return false;
  }
  return tpm_manager_utility_->ClearStoredOwnerPassword();
}

bool TpmImpl::GetVersionInfo(TpmVersionInfo* version_info) {
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

static void ParseIFXFirmwarePackage(
    uint64_t* offset,
    uint8_t* buffer,
    Tpm::IFXFieldUpgradeInfo::FirmwarePackage* firmware_package) {
  Trspi_UnloadBlob_UINT32(offset, &firmware_package->package_id, buffer);
  Trspi_UnloadBlob_UINT32(offset, &firmware_package->version, buffer);
  Trspi_UnloadBlob_UINT32(offset, &firmware_package->stale_version, buffer);
}

bool TpmImpl::GetIFXFieldUpgradeInfo(IFXFieldUpgradeInfo* info) {
  ScopedTssContext context_handle;
  if ((*(context_handle.ptr()) = ConnectContext()) == 0) {
    LOG(ERROR) << "Could not open the TPM";
    return false;
  }

  TSS_HTPM tpm_handle;
  if (!GetTpm(context_handle, &tpm_handle)) {
    LOG(ERROR) << "Could not get a handle to the TPM.";
    return false;
  }

  uint8_t kRequest[] = {0x11, 0x00, 0x00};
  uint32_t response_size;
  ScopedTssMemory response(context_handle);
  if (auto err = CreateError<TPM1Error>(
          Tspi_TPM_FieldUpgrade(tpm_handle, sizeof(kRequest), kRequest,
                                &response_size, response.ptr()))) {
    LOG(ERROR) << "Error calling Tspi_TPM_FieldUpgrade: " << *err;
    return false;
  }

  const uint32_t kFieldUpgradeInfo2Size = 106;
  const uint32_t kFieldUpgradeResponseSize =
      kFieldUpgradeInfo2Size + sizeof(uint16_t);
  if (response_size < kFieldUpgradeResponseSize) {
    LOG(ERROR) << "FieldUpgrade response too short";
    return false;
  }

  // Parse the response.
  uint64_t offset = 0;
  uint16_t size = 0;
  Trspi_UnloadBlob_UINT16(&offset, &size, response.value());

  if (size != kFieldUpgradeInfo2Size) {
    LOG(ERROR) << "FieldUpgrade response size too short";
    return false;
  }

  Trspi_UnloadBlob_UINT16(&offset, NULL, response.value());
  Trspi_UnloadBlob_UINT16(&offset, &info->max_data_size, response.value());
  Trspi_UnloadBlob_UINT16(&offset, NULL, response.value());
  Trspi_UnloadBlob_UINT32(&offset, NULL, response.value());
  offset += 34;
  ParseIFXFirmwarePackage(&offset, response.value(), &info->bootloader);
  Trspi_UnloadBlob_UINT16(&offset, NULL, response.value());
  ParseIFXFirmwarePackage(&offset, response.value(), &info->firmware[0]);
  ParseIFXFirmwarePackage(&offset, response.value(), &info->firmware[1]);
  Trspi_UnloadBlob_UINT16(&offset, &info->status, response.value());
  ParseIFXFirmwarePackage(&offset, response.value(), &info->process_fw);
  Trspi_UnloadBlob_UINT16(&offset, NULL, response.value());
  offset += 6;
  Trspi_UnloadBlob_UINT16(&offset, &info->field_upgrade_counter,
                          response.value());

  CHECK_EQ(offset, kFieldUpgradeResponseSize);

  return true;
}

bool TpmImpl::SetDelegateData(const brillo::Blob& delegate_blob,
                              bool has_reset_lock_permissions) {
  if (delegate_blob.size() == 0) {
    LOG(ERROR) << __func__ << ": Empty blob.";
    return false;
  }

  has_reset_lock_permissions_ = has_reset_lock_permissions;
  UINT64 offset = 0;
  TPM_DELEGATE_OWNER_BLOB ownerBlob;
  // TODO(b/169392230): Fix the potential memory leak while migrating to tpm
  // manager.

  if (auto err =
          CreateError<TPM1Error>(Trspi_UnloadBlob_TPM_DELEGATE_OWNER_BLOB(
              &offset, const_cast<BYTE*>(delegate_blob.data()), &ownerBlob))) {
    LOG(ERROR) << __func__ << ": Failed to unload delegate blob: " << *err;
    return false;
  }

  if (ownerBlob.pub.pcrInfo.pcrSelection.sizeOfSelect > 1 &&
      ownerBlob.pub.pcrInfo.pcrSelection.pcrSelect != nullptr) {
    is_delegate_bound_to_pcr_ =
        (ownerBlob.pub.pcrInfo.pcrSelection.pcrSelect[0] != 0) ||
        (ownerBlob.pub.pcrInfo.pcrSelection.pcrSelect[1] != 0);
  } else {
    LOG(WARNING) << __func__ << ": Unexpected PCR information: "
                 << ownerBlob.pub.pcrInfo.pcrSelection.sizeOfSelect << " (at "
                 << ownerBlob.pub.pcrInfo.pcrSelection.pcrSelect << ").";
    return false;
  }
  return true;
}

base::Optional<bool> TpmImpl::IsDelegateBoundToPcr() {
  if (!SetDelegateDataFromTpmManager()) {
    LOG(WARNING) << __func__
                 << ": failed to call |SetDelegateDataFromTpmManager|.";
  }
  return is_delegate_bound_to_pcr_;
}

bool TpmImpl::DelegateCanResetDACounter() {
  if (!SetDelegateDataFromTpmManager()) {
    LOG(WARNING) << __func__
                 << ": failed to call |SetDelegateDataFromTpmManager|.";
  }
  return has_reset_lock_permissions_;
}

bool TpmImpl::GetRsuDeviceId(std::string* device_id) {
  // Not supported for TPM 1.2.
  return false;
}

LECredentialBackend* TpmImpl::GetLECredentialBackend() {
  // Not implemented in TPM 1.2.
  return nullptr;
}

SignatureSealingBackend* TpmImpl::GetSignatureSealingBackend() {
  return &signature_sealing_backend_;
}

bool TpmImpl::GetDelegate(brillo::Blob* blob,
                          brillo::Blob* secret,
                          bool* has_reset_lock_permissions) {
  blob->clear();
  secret->clear();
  if (last_tpm_manager_data_.owner_delegate().blob().empty() ||
      last_tpm_manager_data_.owner_delegate().secret().empty()) {
    if (!CacheTpmManagerStatus()) {
      LOG(ERROR) << __func__
                 << ": Failed to call |UpdateLocalDataFromTpmManager|.";
      return false;
    }
  }
  const auto& owner_delegate = last_tpm_manager_data_.owner_delegate();
  *blob = brillo::BlobFromString(owner_delegate.blob());
  *secret = brillo::BlobFromString(owner_delegate.secret());
  *has_reset_lock_permissions = owner_delegate.has_reset_lock_permissions();
  return !blob->empty() && !secret->empty();
}

std::map<uint32_t, std::string> TpmImpl::GetPcrMap(
    const std::string& obfuscated_username, bool use_extended_pcr) const {
  std::map<uint32_t, std::string> pcr_map;
  if (use_extended_pcr) {
    SecureBlob starting_value(SHA_DIGEST_LENGTH, 0);
    SecureBlob digest_value = Sha1(SecureBlob::Combine(
        starting_value, Sha1(SecureBlob(obfuscated_username))));
    pcr_map[kTpmSingleUserPCR] = digest_value.to_string();
  } else {
    pcr_map[kTpmSingleUserPCR] = std::string(SHA_DIGEST_LENGTH, 0);
  }

  return pcr_map;
}

bool TpmImpl::InitializeTpmManagerUtility() {
  if (!tpm_manager_utility_) {
    tpm_manager_utility_ = tpm_manager::TpmManagerUtility::GetSingleton();
    if (!tpm_manager_utility_) {
      LOG(ERROR) << __func__ << ": Failed to get TpmManagerUtility singleton!";
    }
  }
  return tpm_manager_utility_ && tpm_manager_utility_->Initialize();
}

bool TpmImpl::CacheTpmManagerStatus() {
  if (!InitializeTpmManagerUtility()) {
    LOG(ERROR) << __func__ << ": Failed to initialize |TpmManagerUtility|.";
    return false;
  }
  return tpm_manager_utility_->GetTpmStatus(&is_enabled_, &is_owned_,
                                            &last_tpm_manager_data_);
}

bool TpmImpl::UpdateLocalDataFromTpmManager() {
  if (!InitializeTpmManagerUtility()) {
    LOG(ERROR) << __func__ << ": Failed to initialize |TpmManagerUtility|.";
    return false;
  }

  bool is_successful = false;
  bool has_received = false;

  // Repeats data copy into |last_tpm_manager_data_|; reasonable trade-off due
  // to low ROI to avoid that.
  bool is_connected = tpm_manager_utility_->GetOwnershipTakenSignalStatus(
      &is_successful, &has_received, &last_tpm_manager_data_);

  // When we need explicitly query tpm status either because the signal is not
  // ready for any reason, or because the signal is not received yet so we need
  // to run it once in case the signal is sent by tpm_manager before already.
  if (!is_connected || !is_successful ||
      (!has_received && shall_cache_tpm_manager_status_)) {
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

bool TpmImpl::SetDelegateDataFromTpmManager() {
  if (has_set_delegate_data_) {
    return true;
  }
  brillo::Blob blob, unused_secret;
  bool has_reset_lock_permissions = false;
  if (GetDelegate(&blob, &unused_secret, &has_reset_lock_permissions)) {
    // Don't log the error at this level but by the called function and the
    // functions that call it.
    has_set_delegate_data_ |= SetDelegateData(blob, has_reset_lock_permissions);
  }
  return has_set_delegate_data_;
}

TPMErrorBase TpmImpl::GetAuthValue(base::Optional<TpmKeyHandle> key_handle,
                                   const SecureBlob& pass_blob,
                                   SecureBlob* auth_value) {
  // For TPM1.2, the |auth_value| should be the same as |pass_blob|.
  *auth_value = pass_blob;
  return nullptr;
}

}  // namespace cryptohome
