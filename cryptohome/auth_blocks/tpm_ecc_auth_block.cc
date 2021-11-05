// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_blocks/tpm_ecc_auth_block.h"

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/callback_helpers.h>
#include <base/check.h>
#include <base/logging.h>
#include <base/optional.h>
#include <brillo/secure_blob.h>
#include <libhwsec/error/tpm_retry_handler.h>

#include "cryptohome/auth_blocks/tpm_auth_block_utils.h"
#include "cryptohome/crypto.h"
#include "cryptohome/crypto/aes.h"
#include "cryptohome/crypto/elliptic_curve_error.h"
#include "cryptohome/crypto/scrypt.h"
#include "cryptohome/crypto/secure_blob_util.h"
#include "cryptohome/crypto/sha.h"
#include "cryptohome/crypto_error.h"
#include "cryptohome/cryptohome_keys_manager.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/tpm.h"
#include "cryptohome/vault_keyset.pb.h"

using hwsec::error::TPMError;
using hwsec::error::TPMErrorBase;
using hwsec::error::TPMRetryAction;
using hwsec_foundation::error::WrapError;

namespace {

// The failure rate of one GetEccAuthValue operation is about 2.33e-10.
// The failure rate of a series of 5 GetEccAuthValue operation is
// about 1.165e-9. Retry 5 times would let the failure rate become 2.146e-45,
// and that should be a reasonable failure rate.
constexpr int kTryCreateMaxRetryCount = 5;

// The time of doing GetEccAuthValue operation on normal TPM2.0 is about
// 50~100ms, 2 rounds should be enough for rate-limiting against PIN brute-force
// attacks.
constexpr int kDefaultEccAuthValueRounds = 2;

struct VendorAuthValueRounds {
  uint32_t tpm_vendor_id;
  uint32_t auth_value_rounds;
};

// Cr50 Vendor ID ("CROS").
constexpr uint32_t kVendorIdCr50 = 0x43524f53;
// Infineon Vendor ID ("IFX").
constexpr uint32_t kVendorIdIfx = 0x49465800;

constexpr VendorAuthValueRounds kVendorAuthValueRoundsList[] = {
    VendorAuthValueRounds{
        .tpm_vendor_id = kVendorIdCr50,
        .auth_value_rounds = 5,
    },
    VendorAuthValueRounds{
        .tpm_vendor_id = kVendorIdIfx,
        .auth_value_rounds = 2,
    },
};

int CalcEccAuthValueRounds(cryptohome::Tpm* tpm) {
  cryptohome::Tpm::TpmVersionInfo version_info = {};
  if (!tpm->GetVersionInfo(&version_info)) {
    LOG(ERROR) << "Failed to get the TPM version info.";
    return kDefaultEccAuthValueRounds;
  }

  for (VendorAuthValueRounds item : kVendorAuthValueRoundsList) {
    if (version_info.manufacturer == item.tpm_vendor_id) {
      return item.auth_value_rounds;
    }
  }
  return kDefaultEccAuthValueRounds;
}

}  // namespace

namespace cryptohome {

TpmEccAuthBlock::TpmEccAuthBlock(Tpm* tpm,
                                 CryptohomeKeysManager* cryptohome_keys_manager)
    : SyncAuthBlock(kTpmBackedEcc),
      tpm_(tpm),
      cryptohome_key_loader_(
          cryptohome_keys_manager->GetKeyLoader(CryptohomeKeyType::kECC)),
      utils_(tpm, cryptohome_key_loader_) {
  CHECK(tpm != nullptr);
  CHECK(cryptohome_key_loader_ != nullptr);

  // Create the scrypt thread.
  // TODO(yich): Create another thread in userdataauth and pass the thread here.
  base::Thread::Options options;
  options.message_pump_type = base::MessagePumpType::IO;
  scrypt_thread_ = std::make_unique<base::Thread>("scrypt_thread");
  scrypt_thread_->StartWithOptions(std::move(options));
  scrypt_task_runner_ = scrypt_thread_->task_runner();
}

CryptoError TpmEccAuthBlock::TryCreate(const AuthInput& auth_input,
                                       AuthBlockState* auth_block_state,
                                       KeyBlobs* key_blobs,
                                       int retry_limit) {
  if (retry_limit == 0) {
    return CryptoError::CE_OTHER_CRYPTO;
  }
  const brillo::SecureBlob& user_input = auth_input.user_input.value();
  const std::string& obfuscated_username =
      auth_input.obfuscated_username.value();

  TpmEccAuthBlockState auth_state;

  // If the cryptohome key isn't loaded, try to load it.
  if (!cryptohome_key_loader_->HasCryptohomeKey()) {
    cryptohome_key_loader_->Init();
  }

  // If the key still isn't loaded, fail the operation.
  if (!cryptohome_key_loader_->HasCryptohomeKey()) {
    LOG(ERROR) << __func__ << ": Failed to load cryptohome key.";
    // Tell user to reboot the device may resolve this issue.
    return CryptoError::CE_TPM_REBOOT;
  }

  // Encrypt the HVKKM using the TPM and the user's passkey. The output is two
  // encrypted blobs, bound to user state in |sealed_hvkkm| and
  // |extended_sealed_hvkkm|, which are stored in the serialized vault keyset.
  TpmKeyHandle cryptohome_key = cryptohome_key_loader_->GetCryptohomeKey();

  auth_state.salt = CreateSecureRandomBlob(CRYPTOHOME_DEFAULT_KEY_SALT_SIZE);

  if (auth_state.salt.value().size() != CRYPTOHOME_DEFAULT_KEY_SALT_SIZE) {
    LOG(ERROR) << __func__ << ": Wrong salt size.";
    return CryptoError::CE_OTHER_CRYPTO;
  }

  // SVKKM: Software Vault Keyset Key Material.
  brillo::SecureBlob svkkm(kDefaultAesKeySize);
  brillo::SecureBlob pass_blob(kDefaultPassBlobSize);
  if (!DeriveSecretsScrypt(user_input, auth_state.salt.value(),
                           {&pass_blob, &svkkm})) {
    LOG(ERROR) << __func__ << ": Failed to derive pass_blob and SVKKM.";
    return CryptoError::CE_OTHER_CRYPTO;
  }

  auth_state.auth_value_rounds = CalcEccAuthValueRounds(tpm_);

  brillo::SecureBlob auth_value = std::move(pass_blob);

  for (int i = 0; i < auth_state.auth_value_rounds.value(); i++) {
    brillo::SecureBlob tmp_value;
    TPMErrorBase err;
    for (int k = 0; k < kTpmDecryptMaxRetries; k++) {
      err = HANDLE_TPM_COMM_ERROR(
          tpm_->GetEccAuthValue(cryptohome_key, auth_value, &tmp_value));
      if (err == nullptr) {
        break;
      }

      LOG(ERROR) << "Failed to get ECC auth value: " << *err;

      auto ecc_err = err->As<EllipticCurveError>();
      if (ecc_err != nullptr &&
          ecc_err->ErrorCode() == EllipticCurveErrorCode::kScalarOutOfRange) {
        // The scalar for EC_POINT multiplication is out of range.
        // We should retry the process again.
        return TryCreate(auth_input, auth_block_state, key_blobs,
                         retry_limit - 1);
      }

      if (err->ToTPMRetryAction() != TPMRetryAction::kLater) {
        return TpmAuthBlockUtils::TPMErrorToCrypto(err);
      }

      // Reload the cryptohome key may resolve this issue.
      // This would be useful when the TPM daemon accidentally restarted and
      // flushed all handles.
      if (!cryptohome_key_loader_->ReloadCryptohomeKey()) {
        LOG(ERROR) << "Unable to reload Cryptohome key while creating "
                      "TpmEccAuthBlock.";
        // Tell user to reboot the device may resolve this issue.
        return CryptoError::CE_TPM_REBOOT;
      }
    }

    if (err != nullptr) {
      LOG(ERROR) << "Failed to get ECC auth value: " << *err;
      // Tell user to reboot the device may resolve this issue.
      return CryptoError::CE_TPM_REBOOT;
    }

    auth_value = std::move(tmp_value);
  }

  // HVKKM: Hardware Vault Keyset Key Material.
  const auto hvkkm = CreateSecureRandomBlob(kDefaultAesKeySize);

  // Check the size of materials size before deriving the VKK.
  if (svkkm.size() != kDefaultAesKeySize) {
    LOG(ERROR) << __func__ << ": Wrong SVKKM size.";
    return CryptoError::CE_OTHER_CRYPTO;
  }
  if (hvkkm.size() != kDefaultAesKeySize) {
    LOG(ERROR) << __func__ << ": Wrong HVKKM size.";
    return CryptoError::CE_OTHER_CRYPTO;
  }

  // Use the Software & Hardware Vault Keyset Key Material to derive the VKK.
  brillo::SecureBlob vkk = Sha256(brillo::SecureBlob::Combine(svkkm, hvkkm));

  // Make sure the size of VKK is correct.
  if (vkk.size() != kDefaultAesKeySize) {
    LOG(ERROR) << __func__ << ": Wrong VKK size.";
    return CryptoError::CE_OTHER_CRYPTO;
  }

  std::map<uint32_t, std::string> default_pcr_map =
      tpm_->GetPcrMap(obfuscated_username, false /* use_extended_pcr */);
  std::map<uint32_t, std::string> extended_pcr_map =
      tpm_->GetPcrMap(obfuscated_username, true /* use_extended_pcr */);

  brillo::SecureBlob sealed_hvkkm;
  TPMErrorBase err = HANDLE_TPM_COMM_ERROR(tpm_->SealToPcrWithAuthorization(
      hvkkm, auth_value, default_pcr_map, &sealed_hvkkm));
  if (err != nullptr) {
    LOG(ERROR) << "Failed to wrap HVKKM with creds: " << *err;
    return TpmAuthBlockUtils::TPMErrorToCrypto(err);
  }

  brillo::SecureBlob extended_sealed_hvkkm;
  err = HANDLE_TPM_COMM_ERROR(tpm_->SealToPcrWithAuthorization(
      hvkkm, auth_value, extended_pcr_map, &extended_sealed_hvkkm));
  if (err != nullptr) {
    LOG(ERROR) << "Failed to wrap hvkkm with creds for extended user state: "
               << *err;
    return TpmAuthBlockUtils::TPMErrorToCrypto(err);
  }

  auth_state.sealed_hvkkm = std::move(sealed_hvkkm);
  auth_state.extended_sealed_hvkkm = std::move(extended_sealed_hvkkm);

  brillo::SecureBlob pub_key_hash;
  err = HANDLE_TPM_COMM_ERROR(
      tpm_->GetPublicKeyHash(cryptohome_key, &pub_key_hash));
  if (err != nullptr) {
    LOG(ERROR) << "Failed to get the TPM public key hash: " << *err;
    return TpmAuthBlockUtils::TPMErrorToCrypto(err);
  } else {
    auth_state.tpm_public_key_hash = std::move(pub_key_hash);
  }

  auth_state.vkk_iv = CreateSecureRandomBlob(kAesBlockSize);

  // Pass back the vkk and vkk_iv so the generic secret wrapping can use it.
  key_blobs->vkk_key = std::move(vkk);
  key_blobs->vkk_iv = auth_state.vkk_iv.value();
  key_blobs->chaps_iv = auth_state.vkk_iv.value();
  *auth_block_state = AuthBlockState{.state = std::move(auth_state)};
  return CryptoError::CE_NONE;
}

CryptoError TpmEccAuthBlock::Create(const AuthInput& auth_input,
                                    AuthBlockState* auth_block_state,
                                    KeyBlobs* key_blobs) {
  return TryCreate(auth_input, auth_block_state, key_blobs,
                   kTryCreateMaxRetryCount);
}

CryptoError TpmEccAuthBlock::Derive(const AuthInput& auth_input,
                                    const AuthBlockState& state,
                                    KeyBlobs* key_out_data) {
  const TpmEccAuthBlockState* auth_state;
  if (!(auth_state = absl::get_if<TpmEccAuthBlockState>(&state.state))) {
    DLOG(FATAL) << "Invalid AuthBlockState";
    return CryptoError::CE_OTHER_CRYPTO;
  }

  // If the cryptohome key isn't loaded, try to load it.
  if (!cryptohome_key_loader_->HasCryptohomeKey()) {
    cryptohome_key_loader_->Init();
  }

  // If the key still isn't loaded, fail the operation.
  if (!cryptohome_key_loader_->HasCryptohomeKey()) {
    LOG(ERROR) << __func__ << ": Failed to load cryptohome key.";
    // Tell user to reboot the device may resolve this issue.
    return CryptoError::CE_TPM_REBOOT;
  }

  brillo::SecureBlob tpm_public_key_hash =
      auth_state->tpm_public_key_hash.value_or(brillo::SecureBlob());

  CryptoError error = utils_.CheckTPMReadiness(
      auth_state->sealed_hvkkm.has_value(),
      auth_state->tpm_public_key_hash.has_value(), tpm_public_key_hash);
  if (error != CryptoError::CE_NONE) {
    return error;
  }

  bool locked_to_single_user = auth_input.locked_to_single_user.value_or(false);
  const brillo::SecureBlob& user_input = auth_input.user_input.value();

  brillo::SecureBlob vkk;
  error = DeriveVkk(locked_to_single_user, user_input, *auth_state, &vkk);

  if (error != CryptoError::CE_NONE) {
    LOG(ERROR) << "Failed to derive VKK.";
    return error;
  }

  key_out_data->vkk_key = std::move(vkk);
  key_out_data->vkk_iv = auth_state->vkk_iv.value();
  key_out_data->chaps_iv = key_out_data->vkk_iv;

  return CryptoError::CE_NONE;
}

CryptoError TpmEccAuthBlock::DeriveVkk(bool locked_to_single_user,
                                       const brillo::SecureBlob& user_input,
                                       const TpmEccAuthBlockState& auth_state,
                                       brillo::SecureBlob* vkk) {
  const brillo::SecureBlob& salt = auth_state.salt.value();

  // HVKKM: Hardware Vault Keyset Key Material.
  const brillo::SecureBlob& sealed_hvkkm =
      locked_to_single_user ? auth_state.extended_sealed_hvkkm.value()
                            : auth_state.sealed_hvkkm.value();

  // SVKKM: Software Vault Keyset Key Material.
  brillo::SecureBlob svkkm(kDefaultAesKeySize);
  brillo::SecureBlob pass_blob(kDefaultPassBlobSize);

  bool derive_result = false;
  ScopedKeyHandle preload_handle;

  {
    // Prepare the parameters for scrypt.
    std::vector<brillo::SecureBlob*> gen_secrets{&pass_blob, &svkkm};
    base::WaitableEvent scrypt_done(
        base::WaitableEvent::ResetPolicy::MANUAL,
        base::WaitableEvent::InitialState::NOT_SIGNALED);

    // Derive secrets on scrypt task runner.
    scrypt_task_runner_->PostTask(
        FROM_HERE,
        base::BindOnce(
            [](const brillo::SecureBlob& user_input,
               const brillo::SecureBlob& salt,
               std::vector<brillo::SecureBlob*> gen_secrets, bool* result,
               base::WaitableEvent* done) {
              *result =
                  DeriveSecretsScrypt(user_input, salt, std::move(gen_secrets));
              done->Signal();
            },
            user_input, salt, gen_secrets, &derive_result, &scrypt_done));

    // The scrypt should be finished before exiting this socope.
    base::ScopedClosureRunner joiner(base::BindOnce(
        [](base::WaitableEvent* scrypt_done) { scrypt_done->Wait(); },
        base::Unretained(&scrypt_done)));

    // Preload the sealed data while deriving secrets in scrypt.
    TPMErrorBase err = HANDLE_TPM_COMM_ERROR(
        tpm_->PreloadSealedData(sealed_hvkkm, &preload_handle));

    if (err != nullptr) {
      LOG(ERROR) << "Failed to preload the sealed data: " << *err;
      return TpmAuthBlockUtils::TPMErrorToCrypto(err);
    }
  }

  if (!derive_result) {
    LOG(ERROR) << "scrypt derivation failed";
    return CryptoError::CE_TPM_CRYPTO;
  }

  if (svkkm.size() != kDefaultAesKeySize) {
    LOG(ERROR) << __func__ << ": Wrong SVKKM size.";
    return CryptoError::CE_TPM_CRYPTO;
  }

  brillo::SecureBlob hvkkm;
  CryptoError error = DeriveHvkkm(locked_to_single_user, std::move(pass_blob),
                                  sealed_hvkkm, &preload_handle,
                                  auth_state.auth_value_rounds.value(), &hvkkm);
  if (error != CryptoError::CE_NONE) {
    LOG(ERROR) << "Failed to derive HVKKM.";
    return error;
  }

  if (hvkkm.size() != kDefaultAesKeySize) {
    LOG(ERROR) << __func__ << ": Wrong HVKKM size.";
    return CryptoError::CE_TPM_CRYPTO;
  }

  // Use the Software & Hardware Vault Keyset Key Material to derive the VKK.
  *vkk = Sha256(brillo::SecureBlob::Combine(svkkm, hvkkm));
  if (vkk->size() != kDefaultAesKeySize) {
    LOG(ERROR) << __func__ << ": Wrong VKK size.";
    return CryptoError::CE_TPM_CRYPTO;
  }

  return CryptoError::CE_NONE;
}

CryptoError TpmEccAuthBlock::DeriveHvkkm(bool locked_to_single_user,
                                         brillo::SecureBlob pass_blob,
                                         const brillo::SecureBlob& sealed_hvkkm,
                                         ScopedKeyHandle* preload_handle,
                                         uint32_t auth_value_rounds,
                                         brillo::SecureBlob* hvkkm) {
  base::Optional<TpmKeyHandle> sealed_hvkkm_handle;
  // The preload handle may be an invalid handle, we should only use it when
  // it's a valid handle.
  if (preload_handle->has_value()) {
    sealed_hvkkm_handle = preload_handle->value();
  }

  brillo::SecureBlob auth_value = std::move(pass_blob);

  TpmKeyHandle cryptohome_key = cryptohome_key_loader_->GetCryptohomeKey();

  ReportTimerStart(kGenerateEccAuthValueTimer);

  for (int i = 0; i < auth_value_rounds; i++) {
    brillo::SecureBlob tmp_value;
    TPMErrorBase err;
    for (int k = 0; k < kTpmDecryptMaxRetries; k++) {
      err = HANDLE_TPM_COMM_ERROR(
          tpm_->GetEccAuthValue(cryptohome_key, auth_value, &tmp_value));
      if (err == nullptr) {
        break;
      }

      LOG(ERROR) << "Failed to get ECC auth value: " << *err;

      if (err->ToTPMRetryAction() != TPMRetryAction::kLater) {
        return TpmAuthBlockUtils::TPMErrorToCrypto(err);
      }

      // Reload the cryptohome key may resolve this issue.
      // This would be useful when the TPM daemon accidentally restarted and
      // flushed all handles.
      if (!cryptohome_key_loader_->ReloadCryptohomeKey()) {
        LOG(ERROR) << "Unable to reload Cryptohome key while decrypting "
                      "TpmEccAuthBlock.";
        // Tell the user to reboot the device may resolve this issue.
        return CryptoError::CE_TPM_REBOOT;
      }
    }

    if (err != nullptr) {
      LOG(ERROR) << "Failed to get ECC auth value: " << *err;
      // Tell the user to reboot the device may resolve this issue.
      return CryptoError::CE_TPM_REBOOT;
    }

    auth_value = std::move(tmp_value);
  }

  ReportTimerStop(kGenerateEccAuthValueTimer);

  std::map<uint32_t, std::string> pcr_map({{kTpmSingleUserPCR, ""}});
  TPMErrorBase err = HANDLE_TPM_COMM_ERROR(tpm_->UnsealWithAuthorization(
      sealed_hvkkm_handle, sealed_hvkkm, auth_value, pcr_map, hvkkm));
  if (err != nullptr) {
    LOG(ERROR) << "Failed to unwrap VKK with creds: " << *err;
    return TpmAuthBlockUtils::TPMErrorToCrypto(err);
  }

  return CryptoError::CE_NONE;
}

}  // namespace cryptohome
