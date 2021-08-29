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
#include "cryptohome/crypto/scrypt.h"
#include "cryptohome/crypto_error.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/cryptorecovery/recovery_crypto.h"
#include "cryptohome/cryptorecovery/recovery_crypto_hsm_cbor_serialization.h"

using cryptohome::cryptorecovery::HsmPayload;
using cryptohome::cryptorecovery::HsmResponsePlainText;
using cryptohome::cryptorecovery::RecoveryCrypto;

namespace cryptohome {

CryptohomeRecoveryAuthBlock::CryptohomeRecoveryAuthBlock()
    : AuthBlock(/*derivation_type=*/kCryptohomeRecovery) {}

base::Optional<AuthBlockState> CryptohomeRecoveryAuthBlock::Create(
    const AuthInput& auth_input, KeyBlobs* key_blobs, CryptoError* error) {
  DCHECK(key_blobs);
  DCHECK(auth_input.salt.has_value());
  const brillo::SecureBlob& salt = auth_input.salt.value();
  DCHECK(auth_input.cryptohome_recovery_auth_input.has_value());
  auto cryptohome_recovery_auth_input =
      auth_input.cryptohome_recovery_auth_input.value();
  DCHECK(cryptohome_recovery_auth_input.mediator_pub_key.has_value());
  const brillo::SecureBlob& mediator_pub_key =
      cryptohome_recovery_auth_input.mediator_pub_key.value();

  std::unique_ptr<RecoveryCrypto> recovery = RecoveryCrypto::Create();
  if (!recovery) {
    PopulateError(error, CryptoError::CE_OTHER_CRYPTO);
    return base::nullopt;
  }

  // Generates HSM payload that would be persisted on a chromebook.
  HsmPayload hsm_payload;
  brillo::SecureBlob destination_share;
  brillo::SecureBlob recovery_key;
  brillo::SecureBlob channel_pub_key;
  brillo::SecureBlob channel_priv_key;
  // TODO(b/184924482): add values like schema version, user id, etc to
  // onboarding_metadata.
  if (!recovery->GenerateHsmPayload(
          mediator_pub_key,
          /*rsa_pub_key=*/brillo::SecureBlob(),
          /*onboarding_metadata=*/brillo::SecureBlob(), &hsm_payload,
          &destination_share, &recovery_key, &channel_pub_key,
          &channel_priv_key)) {
    PopulateError(error, CryptoError::CE_OTHER_CRYPTO);
    return base::nullopt;
  }

  // Generate wrapped keys from the recovery key.
  // TODO(b/184924482): change wrapped keys to USS key after USS is implemented.
  brillo::SecureBlob aes_skey(kDefaultAesKeySize);
  brillo::SecureBlob vkk_iv(kAesBlockSize);
  if (!DeriveSecretsScrypt(recovery_key, salt, {&aes_skey, &vkk_iv})) {
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

  brillo::SecureBlob hsm_payload_cbor;
  if (!SerializeHsmPayloadToCbor(hsm_payload, &hsm_payload_cbor)) {
    PopulateError(error, CryptoError::CE_OTHER_FATAL);
    return base::nullopt;
  }
  auth_state->set_hsm_payload(hsm_payload_cbor.data(), hsm_payload_cbor.size());

  // TODO(b/184924482): wrap the destination share with TPM.
  auth_state->set_plaintext_destination_share(destination_share.data(),
                                              destination_share.size());
  // TODO(b/196192089): store encrypted keys.
  auth_state->set_channel_priv_key(channel_priv_key.data(),
                                   channel_priv_key.size());
  auth_state->set_channel_pub_key(channel_pub_key.data(),
                                  channel_pub_key.size());
  return auth_block_state;
}

bool CryptohomeRecoveryAuthBlock::Derive(const AuthInput& auth_input,
                                         const AuthBlockState& state,
                                         KeyBlobs* key_blobs,
                                         CryptoError* error) {
  DCHECK(key_blobs);
  DCHECK(auth_input.salt.has_value());
  const brillo::SecureBlob& salt = auth_input.salt.value();
  DCHECK(auth_input.cryptohome_recovery_auth_input.has_value());
  auto cryptohome_recovery_auth_input =
      auth_input.cryptohome_recovery_auth_input.value();
  DCHECK(cryptohome_recovery_auth_input.epoch_pub_key.has_value());
  const brillo::SecureBlob& epoch_pub_key =
      cryptohome_recovery_auth_input.epoch_pub_key.value();
  DCHECK(cryptohome_recovery_auth_input.ephemeral_pub_key.has_value());
  const brillo::SecureBlob& ephemeral_pub_key =
      cryptohome_recovery_auth_input.ephemeral_pub_key.value();
  DCHECK(cryptohome_recovery_auth_input.recovery_response.has_value());
  const brillo::SecureBlob& recovery_response_cbor =
      cryptohome_recovery_auth_input.recovery_response.value();

  const AuthBlockState::CryptohomeRecoveryAuthBlockState& auth_state =
      state.cryptohome_recovery_state();
  brillo::SecureBlob plaintext_destination_share(
      auth_state.plaintext_destination_share().begin(),
      auth_state.plaintext_destination_share().end());
  brillo::SecureBlob channel_priv_key(auth_state.channel_priv_key().begin(),
                                      auth_state.channel_priv_key().end());

  std::unique_ptr<RecoveryCrypto> recovery = RecoveryCrypto::Create();
  if (!recovery) {
    PopulateError(error, CryptoError::CE_OTHER_CRYPTO);
    return false;
  }

  HsmResponsePlainText response_plain_text;
  if (!recovery->DecryptResponsePayload(channel_priv_key, epoch_pub_key,
                                        recovery_response_cbor,
                                        &response_plain_text)) {
    PopulateError(error, CryptoError::CE_OTHER_CRYPTO);
    return false;
  }

  brillo::SecureBlob recovery_key;
  if (!recovery->RecoverDestination(
          response_plain_text.dealer_pub_key, plaintext_destination_share,
          ephemeral_pub_key, response_plain_text.mediated_point,
          &recovery_key)) {
    PopulateError(error, CryptoError::CE_OTHER_CRYPTO);
    return false;
  }

  // Generate wrapped keys from the recovery key.
  // TODO(b/184924482): change wrapped keys to USS key after USS is implemented.
  brillo::SecureBlob aes_skey(kDefaultAesKeySize);
  brillo::SecureBlob vkk_iv(kAesBlockSize);
  if (!DeriveSecretsScrypt(recovery_key, salt, {&aes_skey, &vkk_iv})) {
    PopulateError(error, CryptoError::CE_OTHER_FATAL);
    return false;
  }
  key_blobs->vkk_key = aes_skey;
  key_blobs->vkk_iv = vkk_iv;
  key_blobs->chaps_iv = vkk_iv;

  return true;
}

}  // namespace cryptohome
