// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_blocks/tpm_bound_to_pcr_auth_block.h"

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <base/callback_helpers.h>
#include <base/check.h>
#include <base/logging.h>
#include <brillo/secure_blob.h>
#include <libhwsec/status.h>
#include <libhwsec-foundation/crypto/aes.h>
#include <libhwsec-foundation/crypto/scrypt.h>
#include <libhwsec-foundation/crypto/secure_blob_util.h>

#include "cryptohome/auth_blocks/tpm_auth_block_utils.h"
#include "cryptohome/crypto.h"
#include "cryptohome/crypto_error.h"
#include "cryptohome/cryptohome_keys_manager.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/error/location_utils.h"
#include "cryptohome/tpm.h"
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
using hwsec_foundation::kTpmDecryptMaxRetries;
using hwsec_foundation::error::WrapError;
using hwsec_foundation::status::MakeStatus;
using hwsec_foundation::status::OkStatus;
using hwsec_foundation::status::StatusChain;

namespace cryptohome {

TpmBoundToPcrAuthBlock::TpmBoundToPcrAuthBlock(
    hwsec::CryptohomeFrontend* hwsec,
    CryptohomeKeysManager* cryptohome_keys_manager)
    : SyncAuthBlock(kTpmBackedPcrBound),
      hwsec_(hwsec),
      cryptohome_key_loader_(
          cryptohome_keys_manager->GetKeyLoader(CryptohomeKeyType::kRSA)),
      utils_(hwsec, cryptohome_key_loader_) {
  CHECK(hwsec_ != nullptr);
  CHECK(cryptohome_key_loader_ != nullptr);

  // Create the scrypt thread.
  // TODO(yich): Create another thread in userdataauth and passing it to here.
  base::Thread::Options options;
  options.message_pump_type = base::MessagePumpType::IO;
  scrypt_thread_ = std::make_unique<base::Thread>("scrypt_thread");
  scrypt_thread_->StartWithOptions(std::move(options));
  scrypt_task_runner_ = scrypt_thread_->task_runner();
}

CryptoStatus TpmBoundToPcrAuthBlock::Create(const AuthInput& user_input,
                                            AuthBlockState* auth_block_state,
                                            KeyBlobs* key_blobs) {
  if (!user_input.user_input.has_value()) {
    LOG(ERROR) << "Missing user_input";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocTpmBoundToPcrAuthBlockNoUserInputInCreate),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        CryptoError::CE_OTHER_CRYPTO);
  }
  if (!user_input.obfuscated_username.has_value()) {
    LOG(ERROR) << "Missing obfuscated_username";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocTpmBoundToPcrAuthBlockNoUsernameInCreate),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        CryptoError::CE_OTHER_CRYPTO);
  }

  const brillo::SecureBlob& vault_key = user_input.user_input.value();
  brillo::SecureBlob salt =
      CreateSecureRandomBlob(CRYPTOHOME_DEFAULT_KEY_SALT_SIZE);

  const std::string& obfuscated_username =
      user_input.obfuscated_username.value();

  // If the cryptohome key isn't loaded, try to load it.
  if (!cryptohome_key_loader_->HasCryptohomeKey())
    cryptohome_key_loader_->Init();

  // If the key still isn't loaded, fail the operation.
  if (!cryptohome_key_loader_->HasCryptohomeKey()) {
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocTpmBoundToPcrAuthBlockNoCryptohomeKeyInCreate),
        ErrorActionSet({ErrorAction::kReboot, ErrorAction::kRetry,
                        ErrorAction::kPowerwash}),
        CryptoError::CE_TPM_CRYPTO);
  }

  const auto vkk_key = CreateSecureRandomBlob(kDefaultAesKeySize);
  brillo::SecureBlob pass_blob(kDefaultPassBlobSize);
  brillo::SecureBlob vkk_iv(kAesBlockSize);
  if (!DeriveSecretsScrypt(vault_key, salt, {&pass_blob, &vkk_iv})) {
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(
            kLocTpmBoundToPcrAuthBlockScryptDeriveFailedInCreate),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        CryptoError::CE_OTHER_CRYPTO);
  }

  // Encrypt the VKK using the TPM and the user's passkey. The output is two
  // encrypted blobs, sealed to PCR in |tpm_key| and |extended_tpm_key|,
  // which are stored in the serialized vault keyset.
  hwsec::Key cryptohome_key = cryptohome_key_loader_->GetCryptohomeKey();

  hwsec::StatusOr<brillo::SecureBlob> auth_value =
      hwsec_->GetAuthValue(cryptohome_key, pass_blob);
  if (!auth_value.ok()) {
    return MakeStatus<CryptohomeCryptoError>(
               CRYPTOHOME_ERR_LOC(
                   kLocTpmBoundToPcrAuthBlockGetAuthFailedInCreate),
               ErrorActionSet({ErrorAction::kReboot,
                               ErrorAction::kDevCheckUnexpectedState}))
        .Wrap(TpmAuthBlockUtils::TPMErrorToCryptohomeCryptoError(
            std::move(auth_value).status()));
  }

  hwsec::StatusOr<brillo::Blob> tpm_key = hwsec_->SealWithCurrentUser(
      /*current_user=*/std::nullopt, *auth_value, vkk_key);
  if (!tpm_key.ok()) {
    return MakeStatus<CryptohomeCryptoError>(
               CRYPTOHOME_ERR_LOC(
                   kLocTpmBoundToPcrAuthBlockDefaultSealFailedInCreate),
               ErrorActionSet({ErrorAction::kReboot,
                               ErrorAction::kDevCheckUnexpectedState,
                               ErrorAction::kPowerwash}))
        .Wrap(TpmAuthBlockUtils::TPMErrorToCryptohomeCryptoError(
            std::move(tpm_key).status()));
  }

  hwsec::StatusOr<brillo::Blob> extended_tpm_key =
      hwsec_->SealWithCurrentUser(obfuscated_username, *auth_value, vkk_key);
  if (!extended_tpm_key.ok()) {
    return MakeStatus<CryptohomeCryptoError>(
               CRYPTOHOME_ERR_LOC(
                   kLocTpmBoundToPcrAuthBlockExtendedSealFailedInCreate),
               ErrorActionSet({ErrorAction::kReboot,
                               ErrorAction::kDevCheckUnexpectedState,
                               ErrorAction::kPowerwash}))
        .Wrap(TpmAuthBlockUtils::TPMErrorToCryptohomeCryptoError(
            std::move(extended_tpm_key).status()));
  }

  TpmBoundToPcrAuthBlockState tpm_state;

  // Allow this to fail.  It is not absolutely necessary; it allows us to
  // detect a TPM clear.  If this fails due to a transient issue, then on next
  // successful login, the vault keyset will be re-saved anyway.
  hwsec::StatusOr<brillo::Blob> pub_key_hash =
      hwsec_->GetPubkeyHash(cryptohome_key);
  if (!pub_key_hash.ok()) {
    LOG(ERROR) << "Failed to get the TPM public key hash: "
               << pub_key_hash.status();
  } else {
    tpm_state.tpm_public_key_hash =
        brillo::SecureBlob(pub_key_hash->begin(), pub_key_hash->end());
  }

  tpm_state.scrypt_derived = true;
  tpm_state.tpm_key = brillo::SecureBlob(tpm_key->begin(), tpm_key->end());
  tpm_state.extended_tpm_key =
      brillo::SecureBlob(extended_tpm_key->begin(), extended_tpm_key->end());
  tpm_state.salt = std::move(salt);

  // Pass back the vkk_key and vkk_iv so the generic secret wrapping can use it.
  key_blobs->vkk_key = vkk_key;
  // Note that one might expect the IV to be part of the AuthBlockState. But
  // since it's taken from the scrypt output, it's actually created by the auth
  // block, not used to initialize the auth block.
  key_blobs->vkk_iv = vkk_iv;
  key_blobs->chaps_iv = vkk_iv;

  *auth_block_state = AuthBlockState{.state = std::move(tpm_state)};
  return OkStatus<CryptohomeCryptoError>();
}

CryptoStatus TpmBoundToPcrAuthBlock::Derive(const AuthInput& auth_input,
                                            const AuthBlockState& state,
                                            KeyBlobs* key_out_data) {
  if (!auth_input.user_input.has_value()) {
    LOG(ERROR) << "Missing user_input";

    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocTpmBoundToPcrAuthBlockNoUserInputInDerive),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        CryptoError::CE_OTHER_CRYPTO);
  }

  const TpmBoundToPcrAuthBlockState* tpm_state;
  if (!(tpm_state = std::get_if<TpmBoundToPcrAuthBlockState>(&state.state))) {
    LOG(ERROR) << "Invalid AuthBlockState";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocTpmBoundToPcrAuthBlockInvalidBlockStateInDerive),
        ErrorActionSet(
            {ErrorAction::kDevCheckUnexpectedState, ErrorAction::kAuth}),
        CryptoError::CE_OTHER_CRYPTO);
  }

  if (!tpm_state->scrypt_derived.has_value()) {
    LOG(ERROR) << "Invalid TpmBoundToPcrAuthBlockState: missing scrypt_derived";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocTpmBoundToPcrAuthBlockNoScryptDerivedInDerive),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState,
                        ErrorAction::kAuth, ErrorAction::kDeleteVault}),
        CryptoError::CE_OTHER_CRYPTO);
  }
  if (!tpm_state->scrypt_derived.value()) {
    LOG(ERROR) << "All TpmBoundtoPcr operations should be scrypt derived.";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocTpmBoundToPcrAuthBlockNotScryptDerivedInDerive),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState,
                        ErrorAction::kAuth, ErrorAction::kDeleteVault}),
        CryptoError::CE_OTHER_CRYPTO);
  }
  if (!tpm_state->salt.has_value()) {
    LOG(ERROR) << "Invalid TpmBoundToPcrAuthBlockState: missing salt";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocTpmBoundToPcrAuthBlockNoSaltInDerive),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState,
                        ErrorAction::kAuth, ErrorAction::kDeleteVault}),
        CryptoError::CE_OTHER_CRYPTO);
  }
  if (!tpm_state->tpm_key.has_value()) {
    LOG(ERROR) << "Invalid TpmBoundToPcrAuthBlockState: missing tpm_key";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocTpmBoundToPcrAuthBlockNoTpmKeyInDerive),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState,
                        ErrorAction::kAuth, ErrorAction::kDeleteVault}),
        CryptoError::CE_OTHER_CRYPTO);
  }
  if (!tpm_state->extended_tpm_key.has_value()) {
    LOG(ERROR)
        << "Invalid TpmBoundToPcrAuthBlockState: missing extended_tpm_key";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocTpmBoundToPcrAuthBlockNoExtendedTpmKeyInDerive),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState,
                        ErrorAction::kAuth, ErrorAction::kDeleteVault}),
        CryptoError::CE_OTHER_CRYPTO);
  }

  brillo::SecureBlob tpm_public_key_hash =
      tpm_state->tpm_public_key_hash.value_or(brillo::SecureBlob());

  CryptoStatus error = utils_.CheckTPMReadiness(
      tpm_state->tpm_key.has_value(),
      tpm_state->tpm_public_key_hash.has_value(), tpm_public_key_hash);
  if (!error.ok()) {
    return MakeStatus<CryptohomeCryptoError>(
               CRYPTOHOME_ERR_LOC(
                   kLocTpmBoundToPcrAuthBlockTpmNotReadyInDerive))
        .Wrap(std::move(error));
  }

  key_out_data->vkk_iv = brillo::SecureBlob(kAesBlockSize);
  key_out_data->vkk_key = brillo::SecureBlob(kDefaultAesKeySize);

  bool locked_to_single_user = auth_input.locked_to_single_user.value_or(false);
  brillo::SecureBlob salt = tpm_state->salt.value();
  brillo::SecureBlob tpm_key = locked_to_single_user
                                   ? tpm_state->extended_tpm_key.value()
                                   : tpm_state->tpm_key.value();
  error = DecryptTpmBoundToPcr(auth_input.user_input.value(), tpm_key, salt,
                               &key_out_data->vkk_iv.value(),
                               &key_out_data->vkk_key.value());
  if (!error.ok()) {
    if (!tpm_state->tpm_public_key_hash.has_value()) {
      return MakeStatus<CryptohomeCryptoError>(
          CRYPTOHOME_ERR_LOC(kLocTpmBoundToPcrAuthBlockNoPubKeyHashInDerive),
          ErrorActionSet(
              {ErrorAction::kDevCheckUnexpectedState, ErrorAction::kAuth}),
          CryptoError::CE_NO_PUBLIC_KEY_HASH);
    }
    return MakeStatus<CryptohomeCryptoError>(
               CRYPTOHOME_ERR_LOC(
                   kLocTpmBoundToPcrAuthBlockDecryptFailedInDerive),
               ErrorActionSet(
                   {ErrorAction::kIncorrectAuth, ErrorAction::kAuth}))
        .Wrap(std::move(error));
  }

  key_out_data->chaps_iv = key_out_data->vkk_iv;

  return OkStatus<CryptohomeCryptoError>();
}

CryptoStatus TpmBoundToPcrAuthBlock::DecryptTpmBoundToPcr(
    const brillo::SecureBlob& vault_key,
    const brillo::SecureBlob& tpm_key,
    const brillo::SecureBlob& salt,
    brillo::SecureBlob* vkk_iv,
    brillo::SecureBlob* vkk_key) const {
  brillo::Blob sealed_data(tpm_key.begin(), tpm_key.end());

  bool derive_result = false;
  brillo::SecureBlob pass_blob(kDefaultPassBlobSize);
  std::optional<hwsec::ScopedKey> preload_scoped_key;
  {
    // Prepare the parameters for scrypt.
    std::vector<brillo::SecureBlob*> gen_secrets{&pass_blob, vkk_iv};

    base::WaitableEvent done(base::WaitableEvent::ResetPolicy::MANUAL,
                             base::WaitableEvent::InitialState::NOT_SIGNALED);

    // Derive secrets on scrypt task runner.
    scrypt_task_runner_->PostTask(
        FROM_HERE, base::BindOnce(
                       [](const brillo::SecureBlob& passkey,
                          const brillo::SecureBlob& salt,
                          std::vector<brillo::SecureBlob*> gen_secrets,
                          bool* result, base::WaitableEvent* done) {
                         *result = DeriveSecretsScrypt(passkey, salt,
                                                       std::move(gen_secrets));
                         done->Signal();
                       },
                       vault_key, salt, gen_secrets, &derive_result, &done));

    // The scrypt should be finished before exiting this socope.
    base::ScopedClosureRunner joiner(
        base::BindOnce([](base::WaitableEvent* done) { done->Wait(); },
                       base::Unretained(&done)));

    // Preload the sealed data while deriving secrets in scrypt.
    hwsec::StatusOr<std::optional<hwsec::ScopedKey>> preload_data =
        hwsec_->PreloadSealedData(sealed_data);
    if (!preload_data.ok()) {
      LOG(ERROR) << "Failed to preload the sealed data: "
                 << preload_data.status();
      return MakeStatus<CryptohomeCryptoError>(
                 CRYPTOHOME_ERR_LOC(
                     kLocTpmBoundToPcrAuthBlockPreloadFailedInDecrypt),
                 ErrorActionSet({ErrorAction::kReboot,
                                 ErrorAction::kDevCheckUnexpectedState}))
          .Wrap(TpmAuthBlockUtils::TPMErrorToCryptohomeCryptoError(
              std::move(preload_data).status()));
    }

    preload_scoped_key = std::move(*preload_data);
  }

  if (!derive_result) {
    LOG(ERROR) << "scrypt derivation failed";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(
            kLocTpmBoundToPcrAuthBlockScryptDeriveFailedInDecrypt),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        CryptoError::CE_OTHER_CRYPTO);
  }

  // On TPM1.2 devices, preloading sealed data is meaningless and
  // UnsealWithCurrentUser will check the preload_key not containing any
  // value.
  std::optional<hwsec::Key> preload_key;
  if (preload_scoped_key.has_value()) {
    preload_key = preload_scoped_key.value().GetKey();
  }

  hwsec::Key cryptohome_key = cryptohome_key_loader_->GetCryptohomeKey();

  hwsec::StatusOr<brillo::SecureBlob> auth_value =
      hwsec_->GetAuthValue(cryptohome_key, pass_blob);
  if (!auth_value.ok()) {
    LOG(ERROR) << "Failed to get auth value: " << auth_value.status();
    return MakeStatus<CryptohomeCryptoError>(
               CRYPTOHOME_ERR_LOC(
                   kLocTpmBoundToPcrAuthBlockGetAuthValueFailedInDecrypt))
        .Wrap(TpmAuthBlockUtils::TPMErrorToCryptohomeCryptoError(
            std::move(auth_value).status()));
  }

  hwsec::StatusOr<brillo::SecureBlob> unsealed_data =
      hwsec_->UnsealWithCurrentUser(preload_key, *auth_value, sealed_data);
  if (!unsealed_data.ok()) {
    LOG(ERROR) << "Failed to unseal with auth: " << unsealed_data.status();
    return MakeStatus<CryptohomeCryptoError>(
               CRYPTOHOME_ERR_LOC(
                   kLocTpmBoundToPcrAuthBlockUnsealFailedInDecrypt))
        .Wrap(TpmAuthBlockUtils::TPMErrorToCryptohomeCryptoError(
            std::move(unsealed_data).status()));
  }

  *vkk_key = std::move(*unsealed_data);
  return OkStatus<CryptohomeCryptoError>();
}

}  // namespace cryptohome
