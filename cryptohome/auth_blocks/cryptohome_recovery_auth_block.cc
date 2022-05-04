// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_blocks/cryptohome_recovery_auth_block.h"

#include <memory>
#include <utility>
#include <variant>

#include <base/check.h>
#include <base/logging.h>
#include <brillo/secure_blob.h>
#include <libhwsec-foundation/crypto/aes.h>
#include <libhwsec-foundation/crypto/hkdf.h>
#include <libhwsec-foundation/crypto/scrypt.h>
#include <libhwsec-foundation/crypto/secure_blob_util.h>

#include "cryptohome/auth_blocks/auth_block_state.h"
#include "cryptohome/auth_blocks/revocation.h"
#include "cryptohome/crypto_error.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/cryptorecovery/recovery_crypto_fake_tpm_backend_impl.h"
#include "cryptohome/cryptorecovery/recovery_crypto_hsm_cbor_serialization.h"
#include "cryptohome/cryptorecovery/recovery_crypto_impl.h"
#include "cryptohome/error/location_utils.h"
#include "cryptohome/tpm.h"

using cryptohome::cryptorecovery::HsmPayload;
using cryptohome::cryptorecovery::HsmResponsePlainText;
using cryptohome::cryptorecovery::OnboardingMetadata;
using cryptohome::cryptorecovery::RecoveryCryptoImpl;
using cryptohome::error::CryptohomeCryptoError;
using cryptohome::error::ErrorAction;
using cryptohome::error::ErrorActionSet;
using hwsec_foundation::CreateSecureRandomBlob;
using hwsec_foundation::DeriveSecretsScrypt;
using hwsec_foundation::kAesBlockSize;
using hwsec_foundation::kDefaultAesKeySize;
using hwsec_foundation::status::MakeStatus;
using hwsec_foundation::status::OkStatus;
using hwsec_foundation::status::StatusChain;

namespace cryptohome {

CryptohomeRecoveryAuthBlock::CryptohomeRecoveryAuthBlock(
    hwsec::CryptohomeFrontend* hwsec,
    cryptorecovery::RecoveryCryptoTpmBackend* tpm_backend)
    : CryptohomeRecoveryAuthBlock(hwsec, tpm_backend, nullptr) {}

CryptohomeRecoveryAuthBlock::CryptohomeRecoveryAuthBlock(
    hwsec::CryptohomeFrontend* hwsec,
    cryptorecovery::RecoveryCryptoTpmBackend* tpm_backend,
    LECredentialManager* le_manager)
    : SyncAuthBlock(/*derivation_type=*/kCryptohomeRecovery),
      hwsec_(hwsec),
      tpm_backend_(tpm_backend),
      le_manager_(le_manager) {
  DCHECK(hwsec_);
  DCHECK(tpm_backend_);
}

CryptoStatus CryptohomeRecoveryAuthBlock::Create(
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
  std::unique_ptr<RecoveryCryptoImpl> recovery =
      RecoveryCryptoImpl::Create(tpm_backend_);
  if (!recovery) {
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(
            kLocCryptohomeRecoveryAuthBlockCantCreateRecoveryInCreate),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState,
                        ErrorAction::kReboot, ErrorAction::kAuth}),
        CryptoError::CE_OTHER_CRYPTO);
  }

  // Generates HSM payload that would be persisted on a chromebook.
  HsmPayload hsm_payload;
  brillo::SecureBlob rsa_pub_key;
  brillo::SecureBlob destination_share;
  brillo::SecureBlob recovery_key;
  brillo::SecureBlob channel_pub_key;
  brillo::SecureBlob channel_priv_key;
  // TODO(b/184924482): set values in onboarding_metadata.
  OnboardingMetadata onboarding_metadata;
  if (!recovery->GenerateHsmPayload(mediator_pub_key, onboarding_metadata,
                                    &hsm_payload, &rsa_pub_key,
                                    &destination_share, &recovery_key,
                                    &channel_pub_key, &channel_priv_key)) {
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(
            kLocCryptohomeRecoveryAuthBlockGenerateHSMPayloadFailedInCreate),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState,
                        ErrorAction::kReboot, ErrorAction::kAuth}),
        CryptoError::CE_OTHER_CRYPTO);
  }

  // Generate wrapped keys from the recovery key.
  // TODO(b/184924482): change wrapped keys to USS key after USS is implemented.
  brillo::SecureBlob aes_skey(kDefaultAesKeySize);
  brillo::SecureBlob vkk_iv(kAesBlockSize);
  if (!DeriveSecretsScrypt(recovery_key, salt, {&aes_skey, &vkk_iv})) {
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(
            kLocCryptohomeRecoveryAuthBlockScryptDeriveFailedInCreate),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState,
                        ErrorAction::kReboot, ErrorAction::kAuth}),
        CryptoError::CE_OTHER_FATAL);
  }
  key_blobs->vkk_key = aes_skey;
  key_blobs->vkk_iv = vkk_iv;
  key_blobs->chaps_iv = vkk_iv;

  // Save generated data in auth_block_state.
  CryptohomeRecoveryAuthBlockState auth_state;

  brillo::SecureBlob hsm_payload_cbor;
  if (!SerializeHsmPayloadToCbor(hsm_payload, &hsm_payload_cbor)) {
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(
            kLocCryptohomeRecoveryAuthBlockCborConvFailedInCreate),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState,
                        ErrorAction::kReboot, ErrorAction::kAuth}),
        CryptoError::CE_OTHER_FATAL);
  }
  auth_state.hsm_payload = hsm_payload_cbor;

  // TODO(b/184924482): wrap the destination share with TPM.
  auth_state.plaintext_destination_share = destination_share;
  // TODO(b/196192089): store encrypted keys.
  auth_state.channel_priv_key = channel_priv_key;
  auth_state.channel_pub_key = channel_pub_key;
  auth_state.salt = std::move(salt);
  *auth_block_state = AuthBlockState{.state = std::move(auth_state)};

  if (revocation::IsRevocationSupported(hwsec_)) {
    DCHECK(le_manager_);
    RevocationState revocation_state;
    CryptoError err =
        revocation::Create(le_manager_, &revocation_state, key_blobs);
    if (err != CryptoError::CE_NONE) {
      return MakeStatus<CryptohomeCryptoError>(
          CRYPTOHOME_ERR_LOC(
              kLocCryptohomeRecoveryAuthBlockRevocationCreateFailedInCreate),
          ErrorActionSet(
              {ErrorAction::kDevCheckUnexpectedState, ErrorAction::kReboot}),
          err);
    }
    auth_block_state->revocation_state = revocation_state;
  }

  return OkStatus<CryptohomeCryptoError>();
}

CryptoStatus CryptohomeRecoveryAuthBlock::Derive(const AuthInput& auth_input,
                                                 const AuthBlockState& state,
                                                 KeyBlobs* key_blobs) {
  DCHECK(key_blobs);
  const CryptohomeRecoveryAuthBlockState* auth_state;
  if (!(auth_state =
            std::get_if<CryptohomeRecoveryAuthBlockState>(&state.state))) {
    DLOG(FATAL) << "Invalid AuthBlockState";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(
            kLocCryptohomeRecoveryAuthBlockInvalidBlockStateInDerive),
        ErrorActionSet(
            {ErrorAction::kDevCheckUnexpectedState, ErrorAction::kAuth}),
        CryptoError::CE_OTHER_CRYPTO);
  }
  DCHECK(auth_input.cryptohome_recovery_auth_input.has_value());
  auto cryptohome_recovery_auth_input =
      auth_input.cryptohome_recovery_auth_input.value();
  DCHECK(cryptohome_recovery_auth_input.epoch_response.has_value());
  brillo::SecureBlob serialized_epoch_response =
      cryptohome_recovery_auth_input.epoch_response.value();
  DCHECK(cryptohome_recovery_auth_input.ephemeral_pub_key.has_value());
  const brillo::SecureBlob& ephemeral_pub_key =
      cryptohome_recovery_auth_input.ephemeral_pub_key.value();
  DCHECK(cryptohome_recovery_auth_input.recovery_response.has_value());
  brillo::SecureBlob serialized_response_proto =
      cryptohome_recovery_auth_input.recovery_response.value();

  cryptorecovery::CryptoRecoveryEpochResponse epoch_response;
  if (!epoch_response.ParseFromString(serialized_epoch_response.to_string())) {
    LOG(ERROR) << "Failed to parse CryptoRecoveryEpochResponse";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(
            kLocCryptohomeRecoveryAuthBlockCantParseEpochResponseInDerive),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        CryptoError::CE_OTHER_CRYPTO);
  }
  cryptorecovery::CryptoRecoveryRpcResponse response_proto;
  if (!response_proto.ParseFromString(serialized_response_proto.to_string())) {
    LOG(ERROR) << "Failed to parse CryptoRecoveryRpcResponse";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(
            kLocCryptohomeRecoveryAuthBlockCantParseResponseInDerive),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        CryptoError::CE_OTHER_CRYPTO);
  }

  brillo::SecureBlob plaintext_destination_share =
      auth_state->plaintext_destination_share.value();
  brillo::SecureBlob channel_priv_key = auth_state->channel_priv_key.value();
  brillo::SecureBlob salt = auth_state->salt.value();

  std::unique_ptr<RecoveryCryptoImpl> recovery =
      RecoveryCryptoImpl::Create(tpm_backend_);
  if (!recovery) {
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(
            kLocCryptohomeRecoveryAuthBlockCantCreateRecoveryInDerive),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState,
                        ErrorAction::kReboot, ErrorAction::kAuth}),
        CryptoError::CE_OTHER_CRYPTO);
  }

  HsmResponsePlainText response_plain_text;
  if (!recovery->DecryptResponsePayload(channel_priv_key, epoch_response,
                                        response_proto, &response_plain_text)) {
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(
            kLocCryptohomeRecoveryAuthBlockDecryptFailedInDerive),
        ErrorActionSet({ErrorAction::kIncorrectAuth, ErrorAction::kReboot,
                        ErrorAction::kAuth}),
        CryptoError::CE_OTHER_CRYPTO);
  }

  brillo::SecureBlob recovery_key;
  if (!recovery->RecoverDestination(
          response_plain_text.dealer_pub_key,
          response_plain_text.key_auth_value, plaintext_destination_share,
          ephemeral_pub_key, response_plain_text.mediated_point,
          &recovery_key)) {
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(
            kLocCryptohomeRecoveryAuthBlockRecoveryFailedInDerive),
        ErrorActionSet({ErrorAction::kIncorrectAuth, ErrorAction::kReboot,
                        ErrorAction::kAuth}),
        CryptoError::CE_OTHER_CRYPTO);
  }

  // Generate wrapped keys from the recovery key.
  // TODO(b/184924482): change wrapped keys to USS key after USS is implemented.
  brillo::SecureBlob aes_skey(kDefaultAesKeySize);
  brillo::SecureBlob vkk_iv(kAesBlockSize);
  if (!DeriveSecretsScrypt(recovery_key, salt, {&aes_skey, &vkk_iv})) {
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(
            kLocCryptohomeRecoveryAuthBlockScryptDeriveFailedInDerive),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState,
                        ErrorAction::kReboot, ErrorAction::kAuth}),
        CryptoError::CE_OTHER_FATAL);
  }
  key_blobs->vkk_key = aes_skey;
  key_blobs->vkk_iv = vkk_iv;
  key_blobs->chaps_iv = vkk_iv;

  if (state.revocation_state.has_value()) {
    DCHECK(revocation::IsRevocationSupported(hwsec_));
    DCHECK(le_manager_);
    CryptoError crypto_err = revocation::Derive(
        le_manager_, state.revocation_state.value(), key_blobs);
    if (crypto_err != CryptoError::CE_NONE) {
      return MakeStatus<CryptohomeCryptoError>(
          CRYPTOHOME_ERR_LOC(
              kLocCryptohomeRecoveryAuthBlockRevocationDeriveFailedInDerive),
          ErrorActionSet(
              {ErrorAction::kDevCheckUnexpectedState, ErrorAction::kReboot}),
          crypto_err);
    }
  }

  return OkStatus<CryptohomeCryptoError>();
}

}  // namespace cryptohome
