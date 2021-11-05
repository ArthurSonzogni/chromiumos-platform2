// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_blocks/cryptohome_recovery_auth_block.h"

#include <memory>
#include <utility>

#include <absl/types/variant.h>
#include <base/check.h>
#include <base/logging.h>
#include <brillo/secure_blob.h>

#include "cryptohome/auth_blocks/auth_block_state.h"
#include "cryptohome/crypto/aes.h"
#include "cryptohome/crypto/hkdf.h"
#include "cryptohome/crypto/scrypt.h"
#include "cryptohome/crypto/secure_blob_util.h"
#include "cryptohome/crypto_error.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/cryptorecovery/recovery_crypto_hsm_cbor_serialization.h"
#include "cryptohome/cryptorecovery/recovery_crypto_impl.h"

using cryptohome::cryptorecovery::HsmPayload;
using cryptohome::cryptorecovery::HsmResponsePlainText;
using cryptohome::cryptorecovery::RecoveryCryptoImpl;

namespace cryptohome {

CryptohomeRecoveryAuthBlock::CryptohomeRecoveryAuthBlock()
    : SyncAuthBlock(/*derivation_type=*/kCryptohomeRecovery) {}

CryptoError CryptohomeRecoveryAuthBlock::Create(
    const AuthInput& auth_input,
    AuthBlockState* auth_block_state,
    KeyBlobs* key_blobs) {
  DCHECK(key_blobs);
  DCHECK(auth_input.cryptohome_recovery_auth_input.has_value());
  auto cryptohome_recovery_auth_input =
      auth_input.cryptohome_recovery_auth_input.value();
  DCHECK(cryptohome_recovery_auth_input.mediator_pub_key.has_value());

  brillo::SecureBlob salt =
      CreateSecureRandomBlob(CRYPTOHOME_DEFAULT_KEY_SALT_SIZE);

  const brillo::SecureBlob& mediator_pub_key =
      cryptohome_recovery_auth_input.mediator_pub_key.value();

  std::unique_ptr<RecoveryCryptoImpl> recovery = RecoveryCryptoImpl::Create();
  if (!recovery) {
    return CryptoError::CE_OTHER_CRYPTO;
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
    return CryptoError::CE_OTHER_CRYPTO;
  }

  // Generate wrapped keys from the recovery key.
  // TODO(b/184924482): change wrapped keys to USS key after USS is implemented.
  brillo::SecureBlob aes_skey(kDefaultAesKeySize);
  brillo::SecureBlob vkk_iv(kAesBlockSize);
  if (!DeriveSecretsScrypt(recovery_key, salt, {&aes_skey, &vkk_iv})) {
    return CryptoError::CE_OTHER_FATAL;
  }
  key_blobs->vkk_key = aes_skey;
  key_blobs->vkk_iv = vkk_iv;
  key_blobs->chaps_iv = vkk_iv;

  // Save generated data in auth_block_state.
  CryptohomeRecoveryAuthBlockState auth_state;

  brillo::SecureBlob hsm_payload_cbor;
  if (!SerializeHsmPayloadToCbor(hsm_payload, &hsm_payload_cbor)) {
    return CryptoError::CE_OTHER_FATAL;
  }
  auth_state.hsm_payload = hsm_payload_cbor;

  // TODO(b/184924482): wrap the destination share with TPM.
  auth_state.plaintext_destination_share = destination_share;
  // TODO(b/196192089): store encrypted keys.
  auth_state.channel_priv_key = channel_priv_key;
  auth_state.channel_pub_key = channel_pub_key;
  auth_state.salt = std::move(salt);
  *auth_block_state = AuthBlockState{.state = std::move(auth_state)};
  return CryptoError::CE_NONE;
}

CryptoError CryptohomeRecoveryAuthBlock::Derive(const AuthInput& auth_input,
                                                const AuthBlockState& state,
                                                KeyBlobs* key_blobs) {
  DCHECK(key_blobs);
  const CryptohomeRecoveryAuthBlockState* auth_state;
  if (!(auth_state =
            absl::get_if<CryptohomeRecoveryAuthBlockState>(&state.state))) {
    DLOG(FATAL) << "Invalid AuthBlockState";
    return CryptoError::CE_OTHER_CRYPTO;
  }
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

  brillo::SecureBlob plaintext_destination_share =
      auth_state->plaintext_destination_share.value();
  brillo::SecureBlob channel_priv_key = auth_state->channel_priv_key.value();
  brillo::SecureBlob salt = auth_state->salt.value();

  std::unique_ptr<RecoveryCryptoImpl> recovery = RecoveryCryptoImpl::Create();
  if (!recovery) {
    return CryptoError::CE_OTHER_CRYPTO;
  }

  HsmResponsePlainText response_plain_text;
  if (!recovery->DecryptResponsePayload(channel_priv_key, epoch_pub_key,
                                        recovery_response_cbor,
                                        &response_plain_text)) {
    return CryptoError::CE_OTHER_CRYPTO;
  }

  brillo::SecureBlob recovery_key;
  if (!recovery->RecoverDestination(
          response_plain_text.dealer_pub_key, plaintext_destination_share,
          ephemeral_pub_key, response_plain_text.mediated_point,
          &recovery_key)) {
    return CryptoError::CE_OTHER_CRYPTO;
  }

  // Generate wrapped keys from the recovery key.
  // TODO(b/184924482): change wrapped keys to USS key after USS is implemented.
  brillo::SecureBlob aes_skey(kDefaultAesKeySize);
  brillo::SecureBlob vkk_iv(kAesBlockSize);
  if (!DeriveSecretsScrypt(recovery_key, salt, {&aes_skey, &vkk_iv})) {
    return CryptoError::CE_OTHER_FATAL;
  }
  key_blobs->vkk_key = aes_skey;
  key_blobs->vkk_iv = vkk_iv;
  key_blobs->chaps_iv = vkk_iv;

  return CryptoError::CE_NONE;
}

}  // namespace cryptohome
