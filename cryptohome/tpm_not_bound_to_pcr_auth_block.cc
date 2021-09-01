// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/tpm_not_bound_to_pcr_auth_block.h"

#include <map>
#include <string>
#include <utility>

#include <absl/types/variant.h>
#include <base/check.h>
#include <base/logging.h>
#include <base/optional.h>
#include <brillo/secure_blob.h>

#include "cryptohome/crypto.h"
#include "cryptohome/crypto/aes.h"
#include "cryptohome/crypto/hmac.h"
#include "cryptohome/crypto/scrypt.h"
#include "cryptohome/crypto/secure_blob_util.h"
#include "cryptohome/crypto_error.h"
#include "cryptohome/cryptohome_keys_manager.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/tpm.h"
#include "cryptohome/tpm_auth_block_utils.h"
#include "cryptohome/vault_keyset.pb.h"

using hwsec::error::TPMErrorBase;

namespace cryptohome {

TpmNotBoundToPcrAuthBlock::TpmNotBoundToPcrAuthBlock(
    Tpm* tpm, CryptohomeKeysManager* cryptohome_keys_manager)
    : AuthBlock(kTpmBackedNonPcrBound),
      tpm_(tpm),
      cryptohome_key_loader_(
          cryptohome_keys_manager->GetKeyLoader(CryptohomeKeyType::kRSA)),
      utils_(tpm, cryptohome_key_loader_) {
  CHECK(tpm != nullptr);
  CHECK(cryptohome_key_loader_ != nullptr);
}

bool TpmNotBoundToPcrAuthBlock::Derive(const AuthInput& auth_input,
                                       const AuthBlockState& state,
                                       KeyBlobs* key_out_data,
                                       CryptoError* error) {
  const TpmNotBoundToPcrAuthBlockState* tpm_state;
  if (!(tpm_state =
            absl::get_if<TpmNotBoundToPcrAuthBlockState>(&state.state))) {
    DLOG(FATAL) << "Called with an invalid auth block state";
    return false;
  }

  brillo::SecureBlob tpm_public_key_hash;
  if (tpm_state->tpm_public_key_hash.has_value()) {
    tpm_public_key_hash = tpm_state->tpm_public_key_hash.value();
  }

  if (!utils_.CheckTPMReadiness(tpm_state->tpm_key.has_value(),
                                tpm_state->tpm_public_key_hash.has_value(),
                                tpm_public_key_hash, error)) {
    return false;
  }

  key_out_data->vkk_iv = brillo::SecureBlob(kAesBlockSize);
  key_out_data->vkk_key = brillo::SecureBlob(kDefaultAesKeySize);

  brillo::SecureBlob salt;
  if (tpm_state->salt.has_value()) {
    salt = tpm_state->salt.value();
  }
  brillo::SecureBlob tpm_key;
  if (tpm_state->tpm_key.has_value()) {
    tpm_key = tpm_state->tpm_key.value();
  }

  if (!DecryptTpmNotBoundToPcr(
          *tpm_state, auth_input.user_input.value(), tpm_key, salt, error,
          &key_out_data->vkk_iv.value(), &key_out_data->vkk_key.value())) {
    return false;
  }

  key_out_data->chaps_iv = key_out_data->vkk_iv;

  if (tpm_state->wrapped_reset_seed.has_value()) {
    key_out_data->wrapped_reset_seed = tpm_state->wrapped_reset_seed;
  }

  if (!tpm_state->tpm_public_key_hash.has_value() && error) {
    *error = CryptoError::CE_NO_PUBLIC_KEY_HASH;
  }

  return true;
}

base::Optional<AuthBlockState> TpmNotBoundToPcrAuthBlock::Create(
    const AuthInput& user_input, KeyBlobs* key_blobs, CryptoError* error) {
  const brillo::SecureBlob& vault_key = user_input.user_input.value();
  brillo::SecureBlob salt =
      CreateSecureRandomBlob(CRYPTOHOME_DEFAULT_KEY_SALT_SIZE);

  // If the cryptohome key isn't loaded, try to load it.
  if (!cryptohome_key_loader_->HasCryptohomeKey())
    cryptohome_key_loader_->Init();

  // If the key still isn't loaded, fail the operation.
  if (!cryptohome_key_loader_->HasCryptohomeKey())
    return base::nullopt;

  const auto local_blob = CreateSecureRandomBlob(kDefaultAesKeySize);
  brillo::SecureBlob tpm_key;
  brillo::SecureBlob aes_skey(kDefaultAesKeySize);
  brillo::SecureBlob kdf_skey(kDefaultAesKeySize);
  brillo::SecureBlob vkk_iv(kAesBlockSize);
  if (!DeriveSecretsScrypt(vault_key, salt, {&aes_skey, &kdf_skey, &vkk_iv})) {
    return base::nullopt;
  }

  // Encrypt the VKK using the TPM and the user's passkey.  The output is an
  // encrypted blob in tpm_key, which is stored in the serialized vault
  // keyset.
  if (TPMErrorBase err =
          tpm_->EncryptBlob(cryptohome_key_loader_->GetCryptohomeKey(),
                            local_blob, aes_skey, &tpm_key)) {
    LOG(ERROR) << "Failed to wrap vkk with creds: " << *err;
    return base::nullopt;
  }

  TpmNotBoundToPcrAuthBlockState auth_state;
  // Allow this to fail.  It is not absolutely necessary; it allows us to
  // detect a TPM clear.  If this fails due to a transient issue, then on next
  // successful login, the vault keyset will be re-saved anyway.
  brillo::SecureBlob pub_key_hash;
  if (TPMErrorBase err = tpm_->GetPublicKeyHash(
          cryptohome_key_loader_->GetCryptohomeKey(), &pub_key_hash)) {
    LOG(ERROR) << "Failed to get tpm public key hash: " << *err;
  } else {
    auth_state.tpm_public_key_hash = pub_key_hash;
  }

  auth_state.scrypt_derived = true;
  auth_state.tpm_key = tpm_key;
  auth_state.salt = std::move(salt);

  // Pass back the vkk_key and vkk_iv so the generic secret wrapping can use it.
  key_blobs->vkk_key = HmacSha256(kdf_skey, local_blob);
  // Note that one might expect the IV to be part of the AuthBlockState. But
  // since it's taken from the scrypt output, it's actually created by the auth
  // block, not used to initialize the auth block.
  key_blobs->vkk_iv = vkk_iv;
  key_blobs->chaps_iv = vkk_iv;

  AuthBlockState auth_block_state = {.state = std::move(auth_state)};
  return auth_block_state;
}

bool TpmNotBoundToPcrAuthBlock::DecryptTpmNotBoundToPcr(
    const TpmNotBoundToPcrAuthBlockState& tpm_state,
    const brillo::SecureBlob& vault_key,
    const brillo::SecureBlob& tpm_key,
    const brillo::SecureBlob& salt,
    CryptoError* error,
    brillo::SecureBlob* vkk_iv,
    brillo::SecureBlob* vkk_key) const {
  brillo::SecureBlob aes_skey(kDefaultAesKeySize);
  brillo::SecureBlob kdf_skey(kDefaultAesKeySize);
  brillo::SecureBlob local_vault_key(vault_key.begin(), vault_key.end());
  unsigned int rounds = tpm_state.password_rounds.has_value()
                            ? tpm_state.password_rounds.value()
                            : kDefaultLegacyPasswordRounds;

  if (tpm_state.scrypt_derived) {
    if (!DeriveSecretsScrypt(vault_key, salt, {&aes_skey, &kdf_skey, vkk_iv})) {
      PopulateError(error, CryptoError::CE_OTHER_FATAL);
      return false;
    }
  } else {
    PasskeyToAesKey(vault_key, salt, rounds, &aes_skey, NULL);
  }

  for (int i = 0; i < kTpmDecryptMaxRetries; i++) {
    if (TPMErrorBase err = tpm_->DecryptBlob(
            cryptohome_key_loader_->GetCryptohomeKey(), tpm_key, aes_skey,
            std::map<uint32_t, std::string>(), &local_vault_key)) {
      if (!TpmAuthBlockUtils::TPMErrorIsRetriable(err)) {
        LOG(ERROR) << "Failed to unwrap VKK with creds: " << *err;
        ReportCryptohomeError(kDecryptAttemptWithTpmKeyFailed);
        *error = TpmAuthBlockUtils::TPMErrorToCrypto(err);
        return false;
      }

      // If the error is retriable, reload the key first.
      if (!cryptohome_key_loader_->ReloadCryptohomeKey()) {
        LOG(ERROR) << "Unable to reload Cryptohome key.";
        ReportCryptohomeError(kDecryptAttemptWithTpmKeyFailed);
        *error = CryptoError::CE_TPM_CRYPTO;
        return false;
      }
    } else {
      break;
    }
  }

  if (tpm_state.scrypt_derived) {
    *vkk_key = HmacSha256(kdf_skey, local_vault_key);
  } else {
    if (!PasskeyToAesKey(local_vault_key, salt, rounds, vkk_key, vkk_iv)) {
      LOG(ERROR) << "Failure converting IVKK to VKK.";
      PopulateError(error, CryptoError::CE_OTHER_FATAL);
      return false;
    }
  }
  return true;
}

}  // namespace cryptohome
