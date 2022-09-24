// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_blocks/cryptohome_recovery_auth_block.h"

#include <memory>
#include <string>
#include <utility>
#include <variant>

#include <base/check.h>
#include <base/logging.h>
#include <base/notreached.h>
#include <brillo/secure_blob.h>
#include <libhwsec-foundation/crypto/aes.h>
#include <libhwsec-foundation/crypto/hkdf.h>
#include <libhwsec-foundation/crypto/scrypt.h>
#include <libhwsec-foundation/crypto/secure_blob_util.h>

#include "cryptohome/auth_blocks/revocation.h"
#include "cryptohome/crypto_error.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/cryptorecovery/recovery_crypto_hsm_cbor_serialization.h"
#include "cryptohome/cryptorecovery/recovery_crypto_impl.h"
#include "cryptohome/error/location_utils.h"
#include "cryptohome/flatbuffer_schemas/auth_block_state.h"

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
    hwsec::RecoveryCryptoFrontend* recovery_hwsec,
    Platform* platform)
    : CryptohomeRecoveryAuthBlock(hwsec, recovery_hwsec, nullptr, platform) {}

CryptohomeRecoveryAuthBlock::CryptohomeRecoveryAuthBlock(
    hwsec::CryptohomeFrontend* hwsec,
    hwsec::RecoveryCryptoFrontend* recovery_hwsec,
    LECredentialManager* le_manager,
    Platform* platform)
    : SyncAuthBlock(/*derivation_type=*/kCryptohomeRecovery),
      hwsec_(hwsec),
      recovery_hwsec_(recovery_hwsec),
      le_manager_(le_manager),
      platform_(platform) {
  DCHECK(hwsec_);
  DCHECK(recovery_hwsec_);
  DCHECK(platform_);
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

  if (!auth_input.obfuscated_username.has_value()) {
    LOG(ERROR) << "Missing obfuscated_username";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocRecoveryAuthBlockNoUsernameInCreate),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        CryptoError::CE_OTHER_CRYPTO);
  }
  const std::string& obfuscated_username =
      auth_input.obfuscated_username.value();

  const brillo::SecureBlob& mediator_pub_key =
      cryptohome_recovery_auth_input.mediator_pub_key.value();
  std::unique_ptr<RecoveryCryptoImpl> recovery =
      RecoveryCryptoImpl::Create(recovery_hwsec_, platform_);
  if (!recovery) {
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocRecoveryAuthBlockCantCreateRecoveryInCreate),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState,
                        ErrorAction::kReboot, ErrorAction::kAuth}),
        CryptoError::CE_OTHER_CRYPTO);
  }

  // Generates HSM payload that would be persisted on a chromebook.
  // TODO(b/184924482): set values in onboarding_metadata.
  OnboardingMetadata onboarding_metadata;
  cryptorecovery::GenerateHsmPayloadRequest generate_hsm_payload_request(
      {.mediator_pub_key = mediator_pub_key,
       .onboarding_metadata = onboarding_metadata,
       .obfuscated_username = obfuscated_username});
  cryptorecovery::GenerateHsmPayloadResponse generate_hsm_payload_response;
  if (!recovery->GenerateHsmPayload(generate_hsm_payload_request,
                                    &generate_hsm_payload_response)) {
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(
            kLocRecoveryAuthBlockGenerateHSMPayloadFailedInCreate),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState,
                        ErrorAction::kReboot, ErrorAction::kAuth}),
        CryptoError::CE_OTHER_CRYPTO);
  }

  // Generate wrapped keys from the recovery key.
  key_blobs->vkk_key = generate_hsm_payload_response.recovery_key;

  // Save generated data in auth_block_state.
  CryptohomeRecoveryAuthBlockState auth_state;

  brillo::SecureBlob hsm_payload_cbor;
  if (!SerializeHsmPayloadToCbor(generate_hsm_payload_response.hsm_payload,
                                 &hsm_payload_cbor)) {
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocRecoveryAuthBlockCborConvFailedInCreate),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState,
                        ErrorAction::kReboot, ErrorAction::kAuth}),
        CryptoError::CE_OTHER_FATAL);
  }
  auth_state.hsm_payload = hsm_payload_cbor;

  auth_state.encrypted_destination_share =
      generate_hsm_payload_response.encrypted_destination_share;
  auth_state.extended_pcr_bound_destination_share =
      generate_hsm_payload_response.extended_pcr_bound_destination_share;
  auth_state.encrypted_channel_priv_key =
      generate_hsm_payload_response.encrypted_channel_priv_key;
  auth_state.channel_pub_key = generate_hsm_payload_response.channel_pub_key;
  auth_state.encrypted_rsa_priv_key =
      generate_hsm_payload_response.encrypted_rsa_priv_key;
  *auth_block_state = AuthBlockState{.state = std::move(auth_state)};

  if (revocation::IsRevocationSupported(hwsec_)) {
    DCHECK(le_manager_);
    RevocationState revocation_state;
    CryptoStatus result =
        revocation::Create(le_manager_, &revocation_state, key_blobs);
    if (!result.ok()) {
      return MakeStatus<CryptohomeCryptoError>(
                 CRYPTOHOME_ERR_LOC(
                     kLocRecoveryAuthBlockRevocationCreateFailedInCreate))
          .Wrap(std::move(result));
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
        CRYPTOHOME_ERR_LOC(kLocRecoveryAuthBlockInvalidBlockStateInDerive),
        ErrorActionSet(
            {ErrorAction::kDevCheckUnexpectedState, ErrorAction::kAuth}),
        CryptoError::CE_OTHER_CRYPTO);
  }

  if (!auth_input.obfuscated_username.has_value()) {
    LOG(ERROR) << "Missing obfuscated_username";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocRecoveryAuthBlockNoUsernameInDerive),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        CryptoError::CE_OTHER_CRYPTO);
  }
  const std::string& obfuscated_username =
      auth_input.obfuscated_username.value();

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
        CRYPTOHOME_ERR_LOC(kLocRecoveryAuthBlockCantParseEpochResponseInDerive),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        CryptoError::CE_OTHER_CRYPTO);
  }
  cryptorecovery::CryptoRecoveryRpcResponse response_proto;
  if (!response_proto.ParseFromString(serialized_response_proto.to_string())) {
    LOG(ERROR) << "Failed to parse CryptoRecoveryRpcResponse";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocRecoveryAuthBlockCantParseResponseInDerive),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        CryptoError::CE_OTHER_CRYPTO);
  }

  std::unique_ptr<RecoveryCryptoImpl> recovery =
      RecoveryCryptoImpl::Create(recovery_hwsec_, platform_);
  if (!recovery) {
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocRecoveryAuthBlockCantCreateRecoveryInDerive),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState,
                        ErrorAction::kReboot, ErrorAction::kAuth}),
        CryptoError::CE_OTHER_CRYPTO);
  }
  HsmResponsePlainText response_plain_text;
  if (!recovery->DecryptResponsePayload(
          cryptorecovery::DecryptResponsePayloadRequest(
              {.encrypted_channel_priv_key =
                   auth_state->encrypted_channel_priv_key,
               .epoch_response = epoch_response,
               .recovery_response_proto = response_proto,
               .obfuscated_username = obfuscated_username}),
          &response_plain_text)) {
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocRecoveryAuthBlockDecryptFailedInDerive),
        ErrorActionSet({ErrorAction::kRetry, ErrorAction::kAuth}),
        CryptoError::CE_OTHER_CRYPTO);
  }

  brillo::SecureBlob recovery_key;
  if (!recovery->RecoverDestination(
          cryptorecovery::RecoverDestinationRequest(
              {.dealer_pub_key = response_plain_text.dealer_pub_key,
               .key_auth_value = response_plain_text.key_auth_value,
               .encrypted_destination_share =
                   auth_state->encrypted_destination_share,
               .extended_pcr_bound_destination_share =
                   auth_state->extended_pcr_bound_destination_share,
               .ephemeral_pub_key = ephemeral_pub_key,
               .mediated_publisher_pub_key = response_plain_text.mediated_point,
               .obfuscated_username = obfuscated_username}),
          &recovery_key)) {
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocRecoveryAuthBlockRecoveryFailedInDerive),
        ErrorActionSet({ErrorAction::kIncorrectAuth, ErrorAction::kReboot,
                        ErrorAction::kAuth}),
        CryptoError::CE_OTHER_CRYPTO);
  }

  // Generate wrapped keys from the recovery key.
  key_blobs->vkk_key = recovery_key;

  if (state.revocation_state.has_value()) {
    DCHECK(revocation::IsRevocationSupported(hwsec_));
    DCHECK(le_manager_);
    CryptoStatus result = revocation::Derive(
        le_manager_, state.revocation_state.value(), key_blobs);
    if (!result.ok()) {
      return MakeStatus<CryptohomeCryptoError>(
                 CRYPTOHOME_ERR_LOC(
                     kLocRecoveryAuthBlockRevocationDeriveFailedInDerive))
          .Wrap(std::move(result));
    }
  }

  return OkStatus<CryptohomeCryptoError>();
}

CryptoStatus CryptohomeRecoveryAuthBlock::PrepareForRemoval(
    const AuthBlockState& state) {
  CryptoStatus crypto_err = PrepareForRemovalInternal(state);
  if (!crypto_err.ok()) {
    LOG(WARNING) << "PrepareForRemoval failed for cryptohome recovery auth "
                    "block. Error: "
                 << crypto_err;
    ReportPrepareForRemovalResult(AuthBlockType::kCryptohomeRecovery,
                                  crypto_err->local_crypto_error());
    // This error is not fatal, proceed to deleting from disk.
  } else {
    ReportPrepareForRemovalResult(AuthBlockType::kCryptohomeRecovery,
                                  CryptoError::CE_NONE);
  }

  return OkStatus<CryptohomeCryptoError>();
}

CryptoStatus CryptohomeRecoveryAuthBlock::PrepareForRemovalInternal(
    const AuthBlockState& state) {
  if (!std::holds_alternative<CryptohomeRecoveryAuthBlockState>(state.state)) {
    NOTREACHED() << "Invalid AuthBlockState";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(
            kLocRecoveryAuthBlockInvalidStateInPrepareForRemoval),
        ErrorActionSet(
            {ErrorAction::kDevCheckUnexpectedState, ErrorAction::kAuth}),
        CryptoError::CE_OTHER_CRYPTO);
  }

  if (!state.revocation_state.has_value()) {
    // No revocation state means that credentials revocation wasn't used in
    // Create(), so there is nothing to do here. This happens when
    // `revocation::IsRevocationSupported()` is `false`.
    return OkStatus<CryptohomeCryptoError>();
  }

  if (!revocation::IsRevocationSupported(hwsec_)) {
    LOG(ERROR)
        << "Revocation is not supported during recovery auth block removal";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(
            kLocRecoveryAuthBlockNoRevocationInPrepareForRemoval),
        ErrorActionSet(
            {ErrorAction::kDevCheckUnexpectedState, ErrorAction::kReboot}),
        CryptoError::CE_OTHER_CRYPTO);
  }

  if (!le_manager_) {
    LOG(ERROR) << "No LE manager during recovery auth block removal";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocRecoveryAuthBlockNoLEManagerInPrepareForRemoval),
        ErrorActionSet(
            {ErrorAction::kDevCheckUnexpectedState, ErrorAction::kReboot}),
        CryptoError::CE_OTHER_CRYPTO);
  }

  CryptoStatus result =
      revocation::Revoke(AuthBlockType::kCryptohomeRecovery, le_manager_,
                         state.revocation_state.value());
  if (!result.ok()) {
    return MakeStatus<CryptohomeCryptoError>(
               CRYPTOHOME_ERR_LOC(
                   kLocRecoveryAuthBlockRevocationFailedInPrepareForRemoval))
        .Wrap(std::move(result));
  }
  return OkStatus<CryptohomeCryptoError>();
}

}  // namespace cryptohome
