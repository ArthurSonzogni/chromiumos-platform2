// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/cryptohome_recovery_auth_block.h"

#include <memory>

#include <base/check.h>
#include <base/logging.h>
#include <brillo/secure_blob.h>

#include "cryptohome/crypto/aes.h"
#include "cryptohome/crypto/hkdf.h"
#include "cryptohome/crypto/recovery_crypto.h"
#include "cryptohome/crypto/scrypt.h"
#include "cryptohome/crypto_error.h"
#include "cryptohome/cryptohome_metrics.h"

namespace cryptohome {

namespace {

// Converts `RecoveryCrypto::EncryptedMediatorShare` struct to
// `AuthBlockState::CryptohomeRecoveryAuthBlockState::EncryptedMediatorShare`
// proto.
void ConvertEncryptedMediatorShare(
    RecoveryCrypto::EncryptedMediatorShare mediator_share,
    AuthBlockState::CryptohomeRecoveryAuthBlockState::EncryptedMediatorShare*
        out_mediator_share) {
  out_mediator_share->set_tag(mediator_share.tag.data(),
                              mediator_share.tag.size());
  out_mediator_share->set_iv(mediator_share.iv.data(),
                             mediator_share.iv.size());
  out_mediator_share->set_ephemeral_pub_key(
      mediator_share.ephemeral_pub_key.data(),
      mediator_share.ephemeral_pub_key.size());
  out_mediator_share->set_encrypted_data(mediator_share.encrypted_data.data(),
                                         mediator_share.encrypted_data.size());
}

}  // namespace

CryptohomeRecoveryAuthBlock::CryptohomeRecoveryAuthBlock()
    : AuthBlock(/*derivation_type=*/kCryptohomeRecovery) {}

base::Optional<AuthBlockState> CryptohomeRecoveryAuthBlock::Create(
    const AuthInput& auth_input, KeyBlobs* key_blobs, CryptoError* error) {
  DCHECK(key_blobs);
  DCHECK(auth_input.salt.has_value());
  const brillo::SecureBlob& salt = auth_input.salt.value();
  DCHECK(auth_input.mediator_pub_key.has_value());
  const brillo::SecureBlob& mediator_pub_key =
      auth_input.mediator_pub_key.value();

  std::unique_ptr<RecoveryCrypto> recovery = RecoveryCrypto::Create();
  if (!recovery) {
    PopulateError(error, CryptoError::CE_OTHER_CRYPTO);
    return base::nullopt;
  }

  RecoveryCrypto::EncryptedMediatorShare encrypted_mediator_share;
  brillo::SecureBlob destination_share;
  brillo::SecureBlob dealer_pub_key;
  bool generate_shares_result =
      recovery->GenerateShares(mediator_pub_key, &encrypted_mediator_share,
                               &destination_share, &dealer_pub_key);
  if (!generate_shares_result) {
    PopulateError(error, CryptoError::CE_OTHER_CRYPTO);
    return base::nullopt;
  }

  brillo::SecureBlob publisher_pub_key;
  brillo::SecureBlob publisher_recovery_key;
  bool generate_pub_keys_result = recovery->GeneratePublisherKeys(
      dealer_pub_key, &publisher_pub_key, &publisher_recovery_key);
  if (!generate_pub_keys_result) {
    PopulateError(error, CryptoError::CE_OTHER_CRYPTO);
    return base::nullopt;
  }

  // Generate wrapped keys from the recovery key.
  // TODO(b/184924482): change wrapped keys to USS key after USS is implemented.
  brillo::SecureBlob aes_skey(kDefaultAesKeySize);
  brillo::SecureBlob vkk_iv(kAesBlockSize);
  if (!DeriveSecretsScrypt(publisher_recovery_key, salt,
                           {&aes_skey, &vkk_iv})) {
    PopulateError(error, CryptoError::CE_OTHER_FATAL);
    return base::nullopt;
  }
  key_blobs->vkk_key = aes_skey;
  key_blobs->vkk_iv = vkk_iv;
  key_blobs->chaps_iv = vkk_iv;

  // Save generated data in auth_block_state.
  AuthBlockState auth_block_state;
  AuthBlockState::CryptohomeRecoveryAuthBlockState* auth_state =
      auth_block_state.mutable_cryptohome_recovery_state();

  ConvertEncryptedMediatorShare(encrypted_mediator_share,
                                auth_state->mutable_encrypted_mediator_share());
  // TODO(b/184924482): wrap the destination share with TPM.
  auth_state->set_plaintext_destination_share(destination_share.data(),
                                              destination_share.size());
  auth_state->set_publisher_pub_key(publisher_pub_key.data(),
                                    publisher_pub_key.size());
  return auth_block_state;
}

bool CryptohomeRecoveryAuthBlock::Derive(const AuthInput& auth_input,
                                         const AuthBlockState& state,
                                         KeyBlobs* key_blobs,
                                         CryptoError* error) {
  DCHECK(key_blobs);
  DCHECK(auth_input.salt.has_value());
  const brillo::SecureBlob& salt = auth_input.salt.value();
  DCHECK(auth_input.mediated_publisher_pub_key.has_value());
  const brillo::SecureBlob& mediated_publisher_pub_key =
      auth_input.mediated_publisher_pub_key.value();
  const AuthBlockState::CryptohomeRecoveryAuthBlockState& auth_state =
      state.cryptohome_recovery_state();
  brillo::SecureBlob publisher_pub_key(auth_state.publisher_pub_key().begin(),
                                       auth_state.publisher_pub_key().end());
  brillo::SecureBlob plaintext_destination_share(
      auth_state.plaintext_destination_share().begin(),
      auth_state.plaintext_destination_share().end());

  std::unique_ptr<RecoveryCrypto> recovery = RecoveryCrypto::Create();
  if (!recovery) {
    PopulateError(error, CryptoError::CE_OTHER_CRYPTO);
    return false;
  }

  // TODO(b/184924482): unwrap the destination share (when we stop using
  // plaintext_destination_share).
  brillo::SecureBlob destination_recovery_key;
  bool result = recovery->RecoverDestination(
      publisher_pub_key, plaintext_destination_share,
      /*ephemeral_pub_key=*/base::nullopt, mediated_publisher_pub_key,
      &destination_recovery_key);
  if (!result) {
    PopulateError(error, CryptoError::CE_OTHER_CRYPTO);
    return false;
  }

  // Generate wrapped keys from the recovery key.
  // TODO(b/184924482): change wrapped keys to USS key after USS is implemented.
  brillo::SecureBlob aes_skey(kDefaultAesKeySize);
  brillo::SecureBlob vkk_iv(kAesBlockSize);
  if (!DeriveSecretsScrypt(destination_recovery_key, salt,
                           {&aes_skey, &vkk_iv})) {
    PopulateError(error, CryptoError::CE_OTHER_FATAL);
    return false;
  }
  key_blobs->vkk_key = aes_skey;
  key_blobs->vkk_iv = vkk_iv;
  key_blobs->chaps_iv = vkk_iv;

  return true;
}

}  // namespace cryptohome
