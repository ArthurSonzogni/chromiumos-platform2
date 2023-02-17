// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_blocks/fingerprint_auth_block.h"

#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <base/notreached.h>
#include <libhwsec/frontend/cryptohome/frontend.h>
#include <libhwsec/status.h>
#include <libhwsec-foundation/crypto/hmac.h>
#include <libhwsec-foundation/crypto/secure_blob_util.h>

#include "cryptohome/auth_blocks/auth_block.h"
#include "cryptohome/auth_blocks/tpm_auth_block_utils.h"
#include "cryptohome/crypto.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/error/cryptohome_crypto_error.h"
#include "cryptohome/error/cryptohome_le_cred_error.h"
#include "cryptohome/error/location_utils.h"
#include "cryptohome/error/locations.h"
#include "cryptohome/flatbuffer_schemas/auth_block_state.h"
#include "cryptohome/le_credential_manager.h"

namespace cryptohome {

namespace {
using cryptohome::error::CryptohomeCryptoError;
using cryptohome::error::CryptohomeError;
using cryptohome::error::ErrorAction;
using cryptohome::error::ErrorActionSet;
using hwsec_foundation::CreateSecureRandomBlob;
using hwsec_foundation::HmacSha256;
using hwsec_foundation::status::MakeStatus;
using hwsec_foundation::status::OkStatus;

// String used as vector in HMAC operation to derive fek_key from auth stack and
// GSC secrets.
constexpr char kFekKeyHmacData[] = "fek_key";

constexpr uint8_t kFingerprintAuthChannel = 0;
constexpr uint32_t kInfiniteDelay = std::numeric_limits<uint32_t>::max();
constexpr size_t kHeSecretSize = 32;

std::vector<hwsec::OperationPolicySetting> GetValidPoliciesOfUser(
    const ObfuscatedUsername& obfuscated_username) {
  return std::vector<hwsec::OperationPolicySetting>{
      hwsec::OperationPolicySetting{
          .device_config_settings =
              hwsec::DeviceConfigSettings{
                  .current_user =
                      hwsec::DeviceConfigSettings::CurrentUserSetting{
                          .username = std::nullopt,
                      },
              },
      },
      hwsec::OperationPolicySetting{
          .device_config_settings =
              hwsec::DeviceConfigSettings{
                  .current_user =
                      hwsec::DeviceConfigSettings::CurrentUserSetting{
                          .username = *obfuscated_username,
                      },
              },
      },
  };
}

}  // namespace

FingerprintAuthBlock::FingerprintAuthBlock(LECredentialManager* le_manager,
                                           BiometricsAuthBlockService* service)
    : AuthBlock(kBiometrics), le_manager_(le_manager), service_(service) {
  CHECK(le_manager_);
  CHECK(service_);
}

CryptoStatus FingerprintAuthBlock::IsSupported(Crypto& crypto) {
  hwsec::CryptohomeFrontend* frontend = crypto.GetHwsec();
  DCHECK(frontend);
  hwsec::StatusOr<bool> is_ready = frontend->IsReady();
  if (!is_ready.ok()) {
    return MakeStatus<CryptohomeCryptoError>(
               CRYPTOHOME_ERR_LOC(
                   kLocFingerprintAuthBlockHwsecReadyErrorInIsSupported),
               ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}))
        .Wrap(TpmAuthBlockUtils::TPMErrorToCryptohomeCryptoError(
            std::move(is_ready).status()));
  }
  if (!is_ready.value()) {
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocFingerprintAuthBlockHwsecNotReadyInIsSupported),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        CryptoError::CE_OTHER_CRYPTO);
  }

  hwsec::StatusOr<bool> enabled =
      crypto.GetHwsec()->IsBiometricsPinWeaverEnabled();
  if (!enabled.ok()) {
    return MakeStatus<CryptohomeCryptoError>(
               CRYPTOHOME_ERR_LOC(
                   kLocFingerprintAuthBlockPinWeaverCheckFailInIsSupported))
        .Wrap(TpmAuthBlockUtils::TPMErrorToCryptohomeCryptoError(
            std::move(enabled).status()));
  }
  if (!enabled.value()) {
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(
            kLocFingerprintAuthBlockPinWeaverNotEnabledInIsSupported),
        ErrorActionSet({ErrorAction::kAuth}), CryptoError::CE_OTHER_CRYPTO);
  }

  if (!crypto.le_manager()) {
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocFingerprintAuthBlockNullLeManagerInIsSupported),
        ErrorActionSet(
            {ErrorAction::kDevCheckUnexpectedState, ErrorAction::kAuth}),
        CryptoError::CE_OTHER_CRYPTO);
  }

  return OkStatus<CryptohomeCryptoError>();
}

void FingerprintAuthBlock::Create(const AuthInput& auth_input,
                                  CreateCallback callback) {
  if (!auth_input.obfuscated_username.has_value()) {
    LOG(ERROR) << "Missing obfuscated_username.";
    std::move(callback).Run(
        MakeStatus<CryptohomeCryptoError>(
            CRYPTOHOME_ERR_LOC(kLocFingerprintAuthBlockNoUsernameInCreate),
            ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
            CryptoError::CE_OTHER_CRYPTO),
        nullptr, nullptr);
    return;
  }
  // TODO(b/249749541): Create rate-limter if not present.
  if (!auth_input.rate_limiter_label) {
    LOG(ERROR) << "Missing rate_limiter_label.";
    std::move(callback).Run(
        MakeStatus<CryptohomeCryptoError>(
            CRYPTOHOME_ERR_LOC(kLocFingerprintAuthBlockNoRateLimiterInCreate),
            ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
            CryptoError::CE_OTHER_CRYPTO),
        nullptr, nullptr);
    return;
  }
  if (!auth_input.reset_secret.has_value()) {
    LOG(ERROR) << "Missing reset_secret.";
    std::move(callback).Run(
        MakeStatus<CryptohomeCryptoError>(
            CRYPTOHOME_ERR_LOC(kLocFingerprintAuthBlockNoResetSecretInCreate),
            ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
            CryptoError::CE_OTHER_CRYPTO),
        nullptr, nullptr);
    return;
  }
  std::optional<brillo::Blob> nonce = service_->TakeNonce();
  if (!nonce.has_value()) {
    LOG(ERROR) << "Missing nonce, probably meaning there isn't a completed "
                  "enroll session.";
    std::move(callback).Run(
        MakeStatus<CryptohomeCryptoError>(
            CRYPTOHOME_ERR_LOC(kLocFingerprintAuthBlockNoNonceInCreate),
            ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
            CryptoError::CE_OTHER_CRYPTO),
        nullptr, nullptr);
    return;
  }
  // TODO(b/247704971): Use Blob instead of SecureBlob for the StartBioAuth
  // fields.
  LECredStatusOr<LECredentialManager::StartBiometricsAuthReply> reply =
      le_manager_->StartBiometricsAuth(kFingerprintAuthChannel,
                                       *auth_input.rate_limiter_label,
                                       brillo::SecureBlob(*nonce));
  if (!reply.ok()) {
    LOG(ERROR) << "Failed to start biometrics auth with PinWeaver.";
    std::move(callback).Run(
        MakeStatus<CryptohomeCryptoError>(
            CRYPTOHOME_ERR_LOC(
                kLocFingerprintAuthBlockStartBioAuthFailedInCreate))
            .Wrap(std::move(reply).err_status()),
        nullptr, nullptr);
    return;
  }
  // TODO(b/247704971): Use Blob instead of SecureBlob for the StartBioAuth
  // fields.
  BiometricsAuthBlockService::OperationInput input{
      .nonce =
          brillo::Blob(reply->server_nonce.begin(), reply->server_nonce.end()),
      .encrypted_label_seed = brillo::Blob(reply->encrypted_he_secret.begin(),
                                           reply->encrypted_he_secret.end()),
      .iv = brillo::Blob(reply->iv.begin(), reply->iv.end()),
  };
  service_->CreateCredential(
      input, base::BindOnce(&FingerprintAuthBlock::ContinueCreate,
                            weak_factory_.GetWeakPtr(), std::move(callback),
                            *auth_input.obfuscated_username,
                            *auth_input.reset_secret));
}

void FingerprintAuthBlock::Derive(const AuthInput& auth_input,
                                  const AuthBlockState& state,
                                  DeriveCallback callback) {
  NOTREACHED();
}

CryptohomeStatus FingerprintAuthBlock::PrepareForRemoval(
    const AuthBlockState& state) {
  return MakeStatus<CryptohomeCryptoError>(
      CRYPTOHOME_ERR_LOC(
          kLocFingerprintAuthBlockPrepareForRemovalUnimplemented),
      ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
      CryptoError::CE_OTHER_FATAL);
}

void FingerprintAuthBlock::ContinueCreate(
    CreateCallback callback,
    const ObfuscatedUsername& obfuscated_username,
    const brillo::SecureBlob& reset_secret,
    CryptohomeStatusOr<BiometricsAuthBlockService::OperationOutput> output) {
  if (!output.ok()) {
    LOG(ERROR) << "Failed to create biometrics credential.";
    std::move(callback).Run(
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(
                kLocFingerprintAuthBlockCreateCredentialFailedInCreate))
            .Wrap(std::move(output).err_status()),
        nullptr, nullptr);
    return;
  }

  std::vector<hwsec::OperationPolicySetting> policies =
      GetValidPoliciesOfUser(obfuscated_username);

  const auto he_secret = CreateSecureRandomBlob(kHeSecretSize);

  // Use the strictest delay schedule. This is because the rate-limit of a
  // fingerprint credential is guarded by the rate-limiter and not the
  // credential leaf itself. So when properly following the protocol, the
  // credential authentication should never fail.
  std::map<uint32_t, uint32_t> delay_sched{{1, kInfiniteDelay}};

  uint64_t label;
  LECredStatus ret = le_manager_->InsertCredential(
      policies, /*le_secret=*/output->auth_pin,
      /*he_secret=*/he_secret, reset_secret, delay_sched,
      /*expiration_delay=*/std::nullopt, &label);
  if (!ret.ok()) {
    LOG(ERROR) << "Failed to insert the fingerprint PinWeaver credential.";
    std::move(callback).Run(
        MakeStatus<CryptohomeCryptoError>(
            CRYPTOHOME_ERR_LOC(
                kLocFingerprintAuthBlockInsertCredentialFailedInCreate))
            .Wrap(std::move(ret)),
        nullptr, nullptr);
    return;
  }
  // There should be no failing branches below this.
  // Put every step that might fail before creating the PinWeaver leaf, to
  // avoid creating unused leaves whenever possible.

  auto auth_state = std::make_unique<AuthBlockState>();
  FingerprintAuthBlockState fingerprint_auth_state;
  fingerprint_auth_state.template_id = std::move(output->record_id);
  fingerprint_auth_state.gsc_secret_label = label;
  auth_state->state = std::move(fingerprint_auth_state);

  auto key_blobs = std::make_unique<KeyBlobs>();
  auto hmac_key = brillo::SecureBlob::Combine(he_secret, output->auth_secret);
  key_blobs->vkk_key =
      HmacSha256(hmac_key, brillo::BlobFromString(kFekKeyHmacData));
  key_blobs->reset_secret = reset_secret;

  std::move(callback).Run(OkStatus<CryptohomeCryptoError>(),
                          std::move(key_blobs), std::move(auth_state));
}

}  // namespace cryptohome
