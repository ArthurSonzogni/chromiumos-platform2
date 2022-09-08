// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_blocks/tpm_ecc_auth_block.h"

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <base/callback_helpers.h>
#include <base/check.h>
#include <base/logging.h>
#include <brillo/secure_blob.h>
#include <libhwsec/status.h>
#include <libhwsec/error/tpm_retry_handler.h>
#include <libhwsec-foundation/crypto/aes.h>
#include <libhwsec-foundation/crypto/scrypt.h>
#include <libhwsec-foundation/crypto/secure_blob_util.h>
#include <libhwsec-foundation/crypto/sha.h>

#include "cryptohome/auth_blocks/tpm_auth_block_utils.h"
#include "cryptohome/crypto.h"
#include "cryptohome/crypto_error.h"
#include "cryptohome/cryptohome_keys_manager.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/error/location_utils.h"
#include "cryptohome/vault_keyset.pb.h"

using cryptohome::error::CryptohomeCryptoError;
using cryptohome::error::ErrorAction;
using cryptohome::error::ErrorActionSet;
using hwsec::TPMError;
using hwsec::TPMErrorBase;
using hwsec::TPMRetryAction;
using hwsec_foundation::CreateSecureRandomBlob;
using hwsec_foundation::DeriveSecretsScrypt;
using hwsec_foundation::kAesBlockSize;
using hwsec_foundation::kDefaultAesKeySize;
using hwsec_foundation::kDefaultPassBlobSize;
using hwsec_foundation::Sha256;
using hwsec_foundation::error::WrapError;
using hwsec_foundation::status::MakeStatus;
using hwsec_foundation::status::OkStatus;
using hwsec_foundation::status::StatusChain;

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

int CalcEccAuthValueRounds(hwsec::CryptohomeFrontend* hwsec) {
  hwsec::StatusOr<uint32_t> manufacturer = hwsec->GetManufacturer();
  if (!manufacturer.ok()) {
    LOG(ERROR) << "Failed to get the TPM version info: "
               << manufacturer.status();
    return kDefaultEccAuthValueRounds;
  }

  for (VendorAuthValueRounds item : kVendorAuthValueRoundsList) {
    if (*manufacturer == item.tpm_vendor_id) {
      return item.auth_value_rounds;
    }
  }
  return kDefaultEccAuthValueRounds;
}

}  // namespace

namespace cryptohome {

TpmEccAuthBlock::TpmEccAuthBlock(hwsec::CryptohomeFrontend* hwsec,
                                 CryptohomeKeysManager* cryptohome_keys_manager)
    : SyncAuthBlock(kTpmBackedEcc),
      hwsec_(hwsec),
      cryptohome_key_loader_(
          cryptohome_keys_manager->GetKeyLoader(CryptohomeKeyType::kECC)),
      utils_(hwsec, cryptohome_key_loader_) {
  CHECK(hwsec_ != nullptr);
  CHECK(cryptohome_key_loader_ != nullptr);

  // Create the scrypt thread.
  // TODO(yich): Create another thread in userdataauth and pass the thread here.
  base::Thread::Options options;
  options.message_pump_type = base::MessagePumpType::IO;
  scrypt_thread_ = std::make_unique<base::Thread>("scrypt_thread");
  scrypt_thread_->StartWithOptions(std::move(options));
  scrypt_task_runner_ = scrypt_thread_->task_runner();
}

CryptoStatus TpmEccAuthBlock::TryCreate(const AuthInput& auth_input,
                                        AuthBlockState* auth_block_state,
                                        KeyBlobs* key_blobs,
                                        int retry_limit) {
  if (retry_limit == 0) {
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocTpmEccAuthBlockRetryLimitExceededInCreate),
        ErrorActionSet(
            {ErrorAction::kDevCheckUnexpectedState, ErrorAction::kAuth}),
        CryptoError::CE_OTHER_CRYPTO);
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
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocTpmEccAuthBlockCryptohomeKeyLoadFailedInCreate),
        ErrorActionSet({ErrorAction::kReboot}), CryptoError::CE_TPM_REBOOT);
  }

  // Encrypt the HVKKM using the TPM and the user's passkey. The output is two
  // encrypted blobs, bound to user state in |sealed_hvkkm| and
  // |extended_sealed_hvkkm|, which are stored in the serialized vault keyset.
  hwsec::Key cryptohome_key = cryptohome_key_loader_->GetCryptohomeKey();

  auth_state.salt = CreateSecureRandomBlob(CRYPTOHOME_DEFAULT_KEY_SALT_SIZE);

  if (auth_state.salt.value().size() != CRYPTOHOME_DEFAULT_KEY_SALT_SIZE) {
    LOG(ERROR) << __func__ << ": Wrong salt size.";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocTpmEccAuthBlockSaltWrongSizeInCreate),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        CryptoError::CE_OTHER_CRYPTO);
  }

  // SVKKM: Software Vault Keyset Key Material.
  brillo::SecureBlob svkkm(kDefaultAesKeySize);
  brillo::SecureBlob pass_blob(kDefaultPassBlobSize);
  if (!DeriveSecretsScrypt(user_input, auth_state.salt.value(),
                           {&pass_blob, &svkkm})) {
    LOG(ERROR) << __func__ << ": Failed to derive pass_blob and SVKKM.";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocTpmEccAuthBlockSVKKMDerivedFailedInCreate),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        CryptoError::CE_OTHER_CRYPTO);
  }

  auth_state.auth_value_rounds = CalcEccAuthValueRounds(hwsec_);

  brillo::SecureBlob auth_value = std::move(pass_blob);

  for (int i = 0; i < auth_state.auth_value_rounds.value(); i++) {
    hwsec::StatusOr<brillo::SecureBlob> tmp_value =
        hwsec_->GetAuthValue(cryptohome_key, auth_value);
    if (!tmp_value.ok()) {
      if (tmp_value.status()->ToTPMRetryAction() ==
          TPMRetryAction::kEllipticCurveScalarOutOfRange) {
        // The scalar for EC_POINT multiplication is out of range.
        // We should retry the process again.
        return TryCreate(auth_input, auth_block_state, key_blobs,
                         retry_limit - 1);
      }

      return MakeStatus<CryptohomeCryptoError>(
                 CRYPTOHOME_ERR_LOC(
                     kLocTpmEccAuthBlockPersistentGetAuthFailedInCreate),
                 ErrorActionSet({ErrorAction::kReboot,
                                 ErrorAction::kDevCheckUnexpectedState}))
          .Wrap(TpmAuthBlockUtils::TPMErrorToCryptohomeCryptoError(
              std::move(tmp_value).status()));
    }
    auth_value = std::move(*tmp_value);
  }

  // HVKKM: Hardware Vault Keyset Key Material.
  const auto hvkkm = CreateSecureRandomBlob(kDefaultAesKeySize);

  // Check the size of materials size before deriving the VKK.
  if (svkkm.size() != kDefaultAesKeySize) {
    LOG(ERROR) << __func__ << ": Wrong SVKKM size.";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocTpmEccAuthBlockSVKKMWrongSizeInCreate),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        CryptoError::CE_OTHER_CRYPTO);
  }
  if (hvkkm.size() != kDefaultAesKeySize) {
    LOG(ERROR) << __func__ << ": Wrong HVKKM size.";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocTpmEccAuthBlockHVKKMWrongSizeInCreate),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        CryptoError::CE_OTHER_CRYPTO);
  }

  // Use the Software & Hardware Vault Keyset Key Material to derive the VKK.
  brillo::SecureBlob vkk = Sha256(brillo::SecureBlob::Combine(svkkm, hvkkm));

  // Make sure the size of VKK is correct.
  if (vkk.size() != kDefaultAesKeySize) {
    LOG(ERROR) << __func__ << ": Wrong VKK size.";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocTpmEccAuthBlockVKKWrongSizeInCreate),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        CryptoError::CE_OTHER_CRYPTO);
  }

  hwsec::StatusOr<brillo::Blob> sealed_hvkkm = hwsec_->SealWithCurrentUser(
      /*current_user=*/std::nullopt, auth_value, hvkkm);
  if (!sealed_hvkkm.ok()) {
    return MakeStatus<CryptohomeCryptoError>(
               CRYPTOHOME_ERR_LOC(kLocTpmEccAuthBlockHVKKMSealFailedInCreate),
               ErrorActionSet({ErrorAction::kReboot,
                               ErrorAction::kDevCheckUnexpectedState}))
        .Wrap(TpmAuthBlockUtils::TPMErrorToCryptohomeCryptoError(
            std::move(sealed_hvkkm).status()));
  }

  hwsec::StatusOr<brillo::Blob> extended_sealed_hvkkm =
      hwsec_->SealWithCurrentUser(obfuscated_username, auth_value, hvkkm);
  if (!extended_sealed_hvkkm.ok()) {
    return MakeStatus<CryptohomeCryptoError>(
               CRYPTOHOME_ERR_LOC(
                   kLocTpmEccAuthBlockHVKKMExtendedSealFailedInCreate),
               ErrorActionSet({ErrorAction::kReboot,
                               ErrorAction::kDevCheckUnexpectedState}))
        .Wrap(TpmAuthBlockUtils::TPMErrorToCryptohomeCryptoError(
            std::move(extended_sealed_hvkkm).status()));
  }

  auth_state.sealed_hvkkm =
      brillo::SecureBlob(sealed_hvkkm->begin(), sealed_hvkkm->end());
  auth_state.extended_sealed_hvkkm = brillo::SecureBlob(
      extended_sealed_hvkkm->begin(), extended_sealed_hvkkm->end());

  hwsec::StatusOr<brillo::Blob> pub_key_hash =
      hwsec_->GetPubkeyHash(cryptohome_key);
  if (!pub_key_hash.ok()) {
    return MakeStatus<CryptohomeCryptoError>(
               CRYPTOHOME_ERR_LOC(
                   kLocTpmEccAuthBlockGetPubkeyHashFailedInCreate),
               ErrorActionSet({ErrorAction::kReboot,
                               ErrorAction::kDevCheckUnexpectedState}))
        .Wrap(TpmAuthBlockUtils::TPMErrorToCryptohomeCryptoError(
            std::move(pub_key_hash).status()));
  } else {
    auth_state.tpm_public_key_hash =
        brillo::SecureBlob(pub_key_hash->begin(), pub_key_hash->end());
  }

  auth_state.vkk_iv = CreateSecureRandomBlob(kAesBlockSize);

  // Pass back the vkk and vkk_iv so the generic secret wrapping can use it.
  key_blobs->vkk_key = std::move(vkk);
  key_blobs->vkk_iv = auth_state.vkk_iv.value();
  key_blobs->chaps_iv = auth_state.vkk_iv.value();
  *auth_block_state = AuthBlockState{.state = std::move(auth_state)};
  return OkStatus<CryptohomeCryptoError>();
}

CryptoStatus TpmEccAuthBlock::Create(const AuthInput& auth_input,
                                     AuthBlockState* auth_block_state,
                                     KeyBlobs* key_blobs) {
  if (!auth_input.user_input.has_value()) {
    LOG(ERROR) << "Missing user_input";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocTpmEccAuthBlockNoUserInputInCreate),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        CryptoError::CE_OTHER_CRYPTO);
  }
  if (!auth_input.obfuscated_username.has_value()) {
    LOG(ERROR) << "Missing obfuscated_username";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocTpmEccAuthBlockNoUsernameInCreate),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        CryptoError::CE_OTHER_CRYPTO);
  }

  return TryCreate(auth_input, auth_block_state, key_blobs,
                   kTryCreateMaxRetryCount);
}

CryptoStatus TpmEccAuthBlock::Derive(const AuthInput& auth_input,
                                     const AuthBlockState& state,
                                     KeyBlobs* key_out_data) {
  if (!auth_input.user_input.has_value()) {
    LOG(ERROR) << "Missing user_input";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocTpmEccAuthBlockNoUserInputInDerive),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        CryptoError::CE_OTHER_CRYPTO);
  }

  const TpmEccAuthBlockState* auth_state;
  if (!(auth_state = std::get_if<TpmEccAuthBlockState>(&state.state))) {
    DLOG(FATAL) << "Invalid AuthBlockState";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocTpmEccAuthBlockInvalidBlockStateInDerive),
        ErrorActionSet(
            {ErrorAction::kDevCheckUnexpectedState, ErrorAction::kAuth}),
        CryptoError::CE_OTHER_CRYPTO);
  }

  // If the cryptohome key isn't loaded, try to load it.
  if (!cryptohome_key_loader_->HasCryptohomeKey()) {
    cryptohome_key_loader_->Init();
  }

  // If the key still isn't loaded, fail the operation.
  if (!cryptohome_key_loader_->HasCryptohomeKey()) {
    LOG(ERROR) << __func__ << ": Failed to load cryptohome key.";
    // Tell user to reboot the device may resolve this issue.
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocTpmEccAuthBlockLoadKeyFailedInDerive),
        ErrorActionSet({ErrorAction::kReboot}), CryptoError::CE_TPM_REBOOT);
  }

  brillo::SecureBlob tpm_public_key_hash =
      auth_state->tpm_public_key_hash.value_or(brillo::SecureBlob());

  CryptoStatus error = utils_.CheckTPMReadiness(
      auth_state->sealed_hvkkm.has_value(),
      auth_state->tpm_public_key_hash.has_value(), tpm_public_key_hash);
  if (!error.ok()) {
    return MakeStatus<CryptohomeCryptoError>(
               CRYPTOHOME_ERR_LOC(kLocTpmEccAuthBlockTpmNotReadyInDerive))
        .Wrap(std::move(error));
  }

  bool locked_to_single_user = auth_input.locked_to_single_user.value_or(false);
  const brillo::SecureBlob& user_input = auth_input.user_input.value();

  brillo::SecureBlob vkk;
  error = DeriveVkk(locked_to_single_user, user_input, *auth_state, &vkk);

  if (!error.ok()) {
    LOG(ERROR) << "Failed to derive VKK.";
    return MakeStatus<CryptohomeCryptoError>(
               CRYPTOHOME_ERR_LOC(kLocTpmEccAuthBlockCantDeriveVKKInDerive))
        .Wrap(std::move(error));
  }

  key_out_data->vkk_key = std::move(vkk);
  key_out_data->vkk_iv = auth_state->vkk_iv.value();
  key_out_data->chaps_iv = key_out_data->vkk_iv;

  return OkStatus<CryptohomeCryptoError>();
}

CryptoStatus TpmEccAuthBlock::DeriveVkk(bool locked_to_single_user,
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

  std::optional<hwsec::ScopedKey> preload_key;
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

    brillo::Blob sealed_data(sealed_hvkkm.begin(), sealed_hvkkm.end());

    // Preload the sealed data while deriving secrets in scrypt.
    hwsec::StatusOr<std::optional<hwsec::ScopedKey>> preload_data =
        hwsec_->PreloadSealedData(sealed_data);
    if (!preload_data.ok()) {
      return MakeStatus<CryptohomeCryptoError>(
                 CRYPTOHOME_ERR_LOC(
                     kLocTpmEccAuthBlockPreloadFailedInDeriveVKK),
                 ErrorActionSet({ErrorAction::kReboot,
                                 ErrorAction::kDevCheckUnexpectedState}))
          .Wrap(TpmAuthBlockUtils::TPMErrorToCryptohomeCryptoError(
              std::move(preload_data).status()));
    }

    preload_key = std::move(*preload_data);
  }

  if (!derive_result) {
    LOG(ERROR) << "scrypt derivation failed";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocTpmEccAuthBlockScryptDeriveFailedInDeriveVKK),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        CryptoError::CE_TPM_CRYPTO);
  }

  if (svkkm.size() != kDefaultAesKeySize) {
    LOG(ERROR) << __func__ << ": Wrong SVKKM size.";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocTpmEccAuthBlockWrongSVKKMSizeInDeriveVKK),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        CryptoError::CE_TPM_CRYPTO);
  }

  brillo::SecureBlob hvkkm;
  CryptoStatus error =
      DeriveHvkkm(locked_to_single_user, std::move(pass_blob), sealed_hvkkm,
                  preload_key, auth_state.auth_value_rounds.value(), &hvkkm);
  if (!error.ok()) {
    LOG(ERROR) << "Failed to derive HVKKM.";
    return MakeStatus<CryptohomeCryptoError>(
               CRYPTOHOME_ERR_LOC(
                   kLocTpmEccAuthBlockDeriveHVKKMFailedInDeriveVKK))
        .Wrap(std::move(error));
  }

  if (hvkkm.size() != kDefaultAesKeySize) {
    LOG(ERROR) << __func__ << ": Wrong HVKKM size.";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocTpmEccAuthBlockWrongHVKKMSizeInDeriveVKK),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        CryptoError::CE_TPM_CRYPTO);
  }

  // Use the Software & Hardware Vault Keyset Key Material to derive the VKK.
  *vkk = Sha256(brillo::SecureBlob::Combine(svkkm, hvkkm));
  if (vkk->size() != kDefaultAesKeySize) {
    LOG(ERROR) << __func__ << ": Wrong VKK size.";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocTpmEccAuthBlockWrongVKKSizeInDeriveVKK),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        CryptoError::CE_TPM_CRYPTO);
  }

  return OkStatus<CryptohomeCryptoError>();
}

CryptoStatus TpmEccAuthBlock::DeriveHvkkm(
    bool locked_to_single_user,
    brillo::SecureBlob pass_blob,
    const brillo::SecureBlob& sealed_hvkkm,
    const std::optional<hwsec::ScopedKey>& preload_key,
    uint32_t auth_value_rounds,
    brillo::SecureBlob* hvkkm) {
  std::optional<hwsec::Key> sealed_hvkkm_key;
  // The preload handle may be an invalid handle, we should only use it when
  // it's a valid handle.
  if (preload_key.has_value()) {
    sealed_hvkkm_key = preload_key.value().GetKey();
  }

  brillo::SecureBlob auth_value = std::move(pass_blob);

  hwsec::Key cryptohome_key = cryptohome_key_loader_->GetCryptohomeKey();

  ReportTimerStart(kGenerateEccAuthValueTimer);

  for (int i = 0; i < auth_value_rounds; i++) {
    hwsec::StatusOr<brillo::SecureBlob> tmp_value =
        hwsec_->GetAuthValue(cryptohome_key, auth_value);
    if (!tmp_value.ok()) {
      return MakeStatus<CryptohomeCryptoError>(
                 CRYPTOHOME_ERR_LOC(
                     kLocTpmEccAuthBlockGetAuthFailedInDeriveHVKKM),
                 ErrorActionSet({ErrorAction::kReboot,
                                 ErrorAction::kDevCheckUnexpectedState,
                                 ErrorAction::kAuth}))
          .Wrap(TpmAuthBlockUtils::TPMErrorToCryptohomeCryptoError(
              std::move(tmp_value).status()));
    }
    auth_value = std::move(*tmp_value);
  }

  ReportTimerStop(kGenerateEccAuthValueTimer);

  brillo::Blob sealed_data(sealed_hvkkm.begin(), sealed_hvkkm.end());

  hwsec::StatusOr<brillo::SecureBlob> unsealed_data =
      hwsec_->UnsealWithCurrentUser(sealed_hvkkm_key, auth_value, sealed_data);
  if (!unsealed_data.ok()) {
    return MakeStatus<CryptohomeCryptoError>(
               CRYPTOHOME_ERR_LOC(kLocTpmEccAuthBlockUnsealFailedInDeriveHVKKM),
               ErrorActionSet({ErrorAction::kIncorrectAuth}))
        .Wrap(TpmAuthBlockUtils::TPMErrorToCryptohomeCryptoError(
            std::move(unsealed_data).status()));
  }

  *hvkkm = std::move(*unsealed_data);
  return OkStatus<CryptohomeCryptoError>();
}

}  // namespace cryptohome
