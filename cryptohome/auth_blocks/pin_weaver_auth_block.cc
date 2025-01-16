// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_blocks/pin_weaver_auth_block.h"

#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <base/check.h>
#include <base/check_op.h>
#include <base/functional/overloaded.h>
#include <base/logging.h>
#include <base/no_destructor.h>
#include <brillo/secure_blob.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>
#include <libhwsec-foundation/crypto/aes.h>
#include <libhwsec-foundation/crypto/hmac.h>
#include <libhwsec-foundation/crypto/scrypt.h>
#include <libhwsec-foundation/crypto/secure_blob_util.h>
#include <libhwsec-foundation/crypto/sha.h>
#include <libhwsec/frontend/cryptohome/frontend.h>
#include <libhwsec/status.h>

#include "cryptohome/auth_blocks/tpm_auth_block_utils.h"
#include "cryptohome/auth_factor/metadata.h"
#include "cryptohome/crypto.h"
#include "cryptohome/cryptohome_common.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/error/action.h"
#include "cryptohome/error/cryptohome_crypto_error.h"
#include "cryptohome/error/cryptohome_error.h"
#include "cryptohome/error/cryptohome_tpm_error.h"
#include "cryptohome/error/locations.h"
#include "cryptohome/features.h"
#include "cryptohome/flatbuffer_schemas/auth_block_state.h"
#include "cryptohome/flatbuffer_schemas/auth_factor.h"

using ::cryptohome::error::CryptohomeCryptoError;
using ::cryptohome::error::CryptohomeError;
using ::cryptohome::error::CryptohomeTPMError;
using ::cryptohome::error::ErrorActionSet;
using ::cryptohome::error::PossibleAction;
using ::cryptohome::error::PrimaryAction;
using ::hwsec_foundation::CreateRandomBlob;
using ::hwsec_foundation::CreateSecureRandomBlob;
using ::hwsec_foundation::DeriveSecretsScrypt;
using ::hwsec_foundation::HmacSha256;
using ::hwsec_foundation::HmacSha256Kdf;
using ::hwsec_foundation::kAesBlockSize;
using ::hwsec_foundation::kDefaultAesKeySize;
using ::hwsec_foundation::Sha256;
using ::hwsec_foundation::status::MakeStatus;
using ::hwsec_foundation::status::OkStatus;
using ::hwsec_foundation::status::StatusChain;

namespace cryptohome {
namespace {

constexpr int kDefaultSecretSize = 32;

// String used as vector in HMAC operation to derive vkk_seed from High Entropy
// secret.
constexpr char kHESecretHmacData[] = "vkk_seed";

// Constants used to define delay schedules.
constexpr uint32_t kLockoutAttemptLimit = 5;
constexpr uint32_t kInfiniteDelay = std::numeric_limits<uint32_t>::max();

// The type of schedule to select.
enum class DelayScheduleType {
  kPin,
  kPassword,
};

// Select the delay schedule to use for newly created factors.
const DelaySchedule& SelectDelayScheduleForNewFactor(DelayScheduleType type) {
  switch (type) {
    case DelayScheduleType::kPin:
      return PinDelaySchedule();
    case DelayScheduleType::kPassword:
      // For password, always use a delay schedule.
      return PasswordDelaySchedule();
  }
}

// Given the metadata for a factor attempt to determine the type of delay
// schedule it should have. Returns nullopt if the factor does not support
// pinweaver auth blocks.
std::optional<DelayScheduleType> DetermineDelayScheduleType(
    const AuthFactorMetadata& auth_factor_metadata) {
  return std::visit<std::optional<DelayScheduleType>>(
      base::Overloaded{
          [](const PasswordMetadata&) { return DelayScheduleType::kPassword; },
          [](const PinMetadata&) { return DelayScheduleType::kPin; },
          [](const KioskMetadata&) { return DelayScheduleType::kPassword; },
          [](const auto&) { return std::nullopt; }},
      auth_factor_metadata.metadata);
}

}  // namespace

const DelaySchedule& LockoutDelaySchedule() {
  static base::NoDestructor<DelaySchedule> kValue(DelaySchedule{
      {kLockoutAttemptLimit, kInfiniteDelay},
  });
  return *kValue;
}

const DelaySchedule& PinDelaySchedule() {
  static base::NoDestructor<DelaySchedule> kValue(DelaySchedule{
      {4, 30},
      {6, 1 * base::Time::kSecondsPerMinute},
      {9, 10 * base::Time::kSecondsPerMinute},
      {12, 30 * base::Time::kSecondsPerMinute},
      {14, 1 * base::Time::kSecondsPerHour},
      {16, 2 * base::Time::kSecondsPerHour},
      {18, 5 * base::Time::kSecondsPerHour},
      {20, 12 * base::Time::kSecondsPerHour},
  });
  return *kValue;
}

const DelaySchedule& PasswordDelaySchedule() {
  // TODO(b/172221456): finalize the policy.
  static base::NoDestructor<DelaySchedule> kValue(DelaySchedule{
      {9, 30},
      {11, 1 * base::Time::kSecondsPerMinute},
      {14, 10 * base::Time::kSecondsPerMinute},
      {17, 30 * base::Time::kSecondsPerMinute},
      {19, 1 * base::Time::kSecondsPerHour},
      {21, 2 * base::Time::kSecondsPerHour},
      {23, 5 * base::Time::kSecondsPerHour},
      {25, 12 * base::Time::kSecondsPerHour},
  });
  return *kValue;
}

CryptoStatus PinWeaverAuthBlock::IsSupported(
    const hwsec::CryptohomeFrontend& hwsec) {
  hwsec::StatusOr<bool> is_ready = hwsec.IsReady();
  if (!is_ready.ok()) {
    return MakeStatus<CryptohomeCryptoError>(
               CRYPTOHOME_ERR_LOC(
                   kLocPinWeaverAuthBlockHwsecReadyErrorInIsSupported),
               ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}))
        .Wrap(TpmAuthBlockUtils::TPMErrorToCryptohomeCryptoError(
            std::move(is_ready).err_status()));
  }
  if (!is_ready.value()) {
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocPinWeaverAuthBlockHwsecNotReadyInIsSupported),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        CryptoError::CE_OTHER_CRYPTO);
  }

  hwsec::StatusOr<bool> has_pinweaver = hwsec.IsPinWeaverEnabled();
  if (!has_pinweaver.ok()) {
    return MakeStatus<CryptohomeCryptoError>(
               CRYPTOHOME_ERR_LOC(
                   kLocPinWeaverAuthBlockPinWeaverCheckFailInIsSupported))
        .Wrap(TpmAuthBlockUtils::TPMErrorToCryptohomeCryptoError(
            std::move(has_pinweaver).err_status()));
  }
  if (!has_pinweaver.value()) {
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocPinWeaverAuthBlockNoPinWeaverInIsSupported),
        ErrorActionSet({PossibleAction::kAuth}), CryptoError::CE_OTHER_CRYPTO);
  }

  return OkStatus<CryptohomeCryptoError>();
}

std::unique_ptr<AuthBlock> PinWeaverAuthBlock::New(
    const hwsec::PinWeaverManagerFrontend& hwsec_pw_manager) {
  return std::make_unique<PinWeaverAuthBlock>(&hwsec_pw_manager);
}

PinWeaverAuthBlock::PinWeaverAuthBlock(
    const hwsec::PinWeaverManagerFrontend* hwsec_pw_manager)
    : AuthBlock(kLowEntropyCredential), hwsec_pw_manager_(hwsec_pw_manager) {
  CHECK(hwsec_pw_manager_);
}

void PinWeaverAuthBlock::Create(const AuthInput& auth_input,
                                const AuthFactorMetadata& auth_factor_metadata,
                                CreateCallback callback) {
  if (!auth_input.user_input.has_value()) {
    LOG(ERROR) << "Missing user_input";
    std::move(callback).Run(
        MakeStatus<CryptohomeCryptoError>(
            CRYPTOHOME_ERR_LOC(kLocPinWeaverAuthBlockNoUserInputInCreate),
            ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
            CryptoError::CE_OTHER_CRYPTO),
        nullptr, nullptr);
    return;
  }
  if (!auth_input.obfuscated_username.has_value()) {
    LOG(ERROR) << "Missing obfuscated_username";
    std::move(callback).Run(
        MakeStatus<CryptohomeCryptoError>(
            CRYPTOHOME_ERR_LOC(kLocPinWeaverAuthBlockNoUsernameInCreate),
            ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
            CryptoError::CE_OTHER_CRYPTO),
        nullptr, nullptr);
    return;
  }
  if (!auth_input.reset_secret.has_value() &&
      !auth_input.reset_seed.has_value()) {
    LOG(ERROR) << "Missing reset_secret or reset_seed";
    std::move(callback).Run(
        MakeStatus<CryptohomeCryptoError>(
            CRYPTOHOME_ERR_LOC(
                kLocPinWeaverAuthBlockNoResetSecretOrResetSeedInCreate),
            ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
            CryptoError::CE_OTHER_CRYPTO),
        nullptr, nullptr);
    return;
  }

  // Determine if the input is a password or PIN, or fail otherwise.
  auto schedule_type = DetermineDelayScheduleType(auth_factor_metadata);
  if (!schedule_type) {
    LOG(ERROR) << "Pinweaver only supports PIN and password factors";
    std::move(callback).Run(
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(
                kLocPinWeaverAuthBlockCreateAuthFactorIsNotSupportedType),
            ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT),
        nullptr, nullptr);
    return;
  }

  PinWeaverAuthBlockState pin_auth_state;
  pin_auth_state.reset_salt = auth_input.reset_salt.has_value()
                                  ? auth_input.reset_salt.value()
                                  : CreateRandomBlob(kAesBlockSize);
  brillo::SecureBlob reset_secret;
  if (auth_input.reset_secret.has_value()) {
    // This case be used for USS as we do not have the concept of reset seed and
    // salt there.
    LOG(INFO) << "PinWeaverAuthBlock: ResetSecret from the AuthInput is passed "
                 "to KeyBlobs.";
    reset_secret = auth_input.reset_secret.value();
  } else {
    // At this point we know auth_input reset_seed is set. The expectation is
    // that this branch of code would be deprecated once we move fully to USS
    // world.
    LOG(INFO) << "PinWeaverAuthBlock: ResetSecret is derived from the "
                 "reset_seed and passed to KeyBlobs.";
    reset_secret = HmacSha256Kdf(pin_auth_state.reset_salt.value(),
                                 auth_input.reset_seed.value());
  }

  brillo::SecureBlob le_secret(kDefaultSecretSize);
  brillo::SecureBlob kdf_skey(kDefaultSecretSize);
  brillo::Blob salt = CreateRandomBlob(kCryptohomeDefaultKeySaltSize);
  if (!DeriveSecretsScrypt(auth_input.user_input.value(), salt,
                           {&le_secret, &kdf_skey})) {
    std::move(callback).Run(
        MakeStatus<CryptohomeCryptoError>(
            CRYPTOHOME_ERR_LOC(
                kLocPinWeaverAuthBlockScryptDeriveFailedInCreate),
            ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
            CryptoError::CE_OTHER_CRYPTO),
        nullptr, nullptr);
    return;
  }

  // Create a randomly generated high entropy secret, derive VKKSeed from it,
  // and use that to generate a VKK. The High Entropy secret will be stored in
  // the LECredentialManager, along with the LE secret (which is |le_secret|
  // here).
  brillo::SecureBlob he_secret = CreateSecureRandomBlob(kDefaultSecretSize);

  // Derive the VKK_seed by performing an HMAC on he_secret.
  brillo::SecureBlob vkk_seed =
      HmacSha256(he_secret, brillo::BlobFromString(kHESecretHmacData));

  // Generate and store random new IVs for file-encryption keys and
  // chaps key encryption.
  const auto fek_iv = CreateRandomBlob(kAesBlockSize);
  const auto chaps_iv = CreateRandomBlob(kAesBlockSize);

  brillo::SecureBlob vkk_key = HmacSha256(kdf_skey, vkk_seed);
  auto key_blobs = std::make_unique<KeyBlobs>();
  auto auth_block_state = std::make_unique<AuthBlockState>();

  key_blobs->vkk_key = vkk_key;
  key_blobs->vkk_iv = fek_iv;
  key_blobs->chaps_iv = chaps_iv;
  key_blobs->reset_secret = reset_secret;
  // Once we are able to correctly set up the VaultKeyset encryption,
  // store the Low Entropy and High Entropy credential in the
  // LECredentialManager.

  // Select the appropriate delay schedule to use.
  const auto& delay_sched = SelectDelayScheduleForNewFactor(*schedule_type);

  std::vector<hwsec::OperationPolicySetting> policies = {
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
                          .username = *auth_input.obfuscated_username.value(),
                      },
              },
      },
  };

  hwsec::StatusOr<uint64_t> result = hwsec_pw_manager_->InsertCredential(
      policies, le_secret, he_secret, reset_secret, delay_sched,
      /*expiration_delay=*/std::nullopt);
  if (!result.ok()) {
    LOG(ERROR) << "Failed to insert credential with PinWeaver.";
    std::move(callback).Run(
        MakeStatus<CryptohomeCryptoError>(
            CRYPTOHOME_ERR_LOC(
                kLocPinWeaverAuthBlockInsertCredentialFailedInCreate))
            .Wrap(
                MakeStatus<CryptohomeTPMError>(std::move(result).err_status())),
        nullptr, nullptr);
    return;
  }

  pin_auth_state.le_label = result.value();
  pin_auth_state.salt = std::move(salt);
  auth_block_state->state = std::move(pin_auth_state);

  std::move(callback).Run(OkStatus<CryptohomeCryptoError>(),
                          std::move(key_blobs), std::move(auth_block_state));
}

void PinWeaverAuthBlock::Derive(const AuthInput& auth_input,
                                const AuthFactorMetadata& auth_factor_metadata,
                                const AuthBlockState& state,
                                DeriveCallback callback) {
  if (!auth_input.user_input.has_value()) {
    LOG(ERROR) << "Missing user_input";
    std::move(callback).Run(
        MakeStatus<CryptohomeCryptoError>(
            CRYPTOHOME_ERR_LOC(kLocPinWeaverAuthBlockNoUserInputInDerive),
            ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
            CryptoError::CE_OTHER_CRYPTO),
        nullptr, std::nullopt);
    return;
  }

  // Determine if the input is a password or PIN, or fail otherwise.
  auto schedule_type = DetermineDelayScheduleType(auth_factor_metadata);
  if (!schedule_type) {
    LOG(ERROR) << "Pinweaver only supports PIN and password factors";
    std::move(callback).Run(
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(
                kLocPinWeaverAuthBlockDeriveAuthFactorIsNotSupportedType),
            ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT),
        nullptr, std::nullopt);
    return;
  }

  const PinWeaverAuthBlockState* auth_state;
  if (!(auth_state = std::get_if<PinWeaverAuthBlockState>(&state.state))) {
    LOG(ERROR) << "Invalid AuthBlockState";
    std::move(callback).Run(
        MakeStatus<CryptohomeCryptoError>(
            CRYPTOHOME_ERR_LOC(kLocPinWeaverAuthBlockInvalidBlockStateInDerive),
            ErrorActionSet({PossibleAction::kDevCheckUnexpectedState,
                            PossibleAction::kAuth}),
            CryptoError::CE_OTHER_CRYPTO),
        nullptr, std::nullopt);
    return;
  }

  brillo::SecureBlob le_secret(kDefaultAesKeySize);
  brillo::SecureBlob kdf_skey(kDefaultAesKeySize);
  if (!auth_state->le_label.has_value()) {
    LOG(ERROR) << "Invalid PinWeaverAuthBlockState: missing le_label";
    std::move(callback).Run(
        MakeStatus<CryptohomeCryptoError>(
            CRYPTOHOME_ERR_LOC(kLocPinWeaverAuthBlockNoLabelInDerive),
            ErrorActionSet({PossibleAction::kDevCheckUnexpectedState,
                            PossibleAction::kAuth,
                            PossibleAction::kDeleteVault}),
            CryptoError::CE_OTHER_CRYPTO),
        nullptr, std::nullopt);
    return;
  }
  if (!auth_state->salt.has_value()) {
    LOG(ERROR) << "Invalid PinWeaverAuthBlockState: missing salt";
    std::move(callback).Run(
        MakeStatus<CryptohomeCryptoError>(
            CRYPTOHOME_ERR_LOC(kLocPinWeaverAuthBlockNoSaltInDerive),
            ErrorActionSet({PossibleAction::kDevCheckUnexpectedState,
                            PossibleAction::kAuth,
                            PossibleAction::kDeleteVault}),
            CryptoError::CE_OTHER_CRYPTO),
        nullptr, std::nullopt);
    return;
  }
  brillo::Blob salt = auth_state->salt.value();
  if (!DeriveSecretsScrypt(auth_input.user_input.value(), salt,
                           {&le_secret, &kdf_skey})) {
    std::move(callback).Run(
        MakeStatus<CryptohomeCryptoError>(
            CRYPTOHOME_ERR_LOC(
                kLocPinWeaverAuthBlockDeriveScryptFailedInDerive),
            ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
            CryptoError::CE_OTHER_FATAL),
        nullptr, std::nullopt);
    return;
  }
  auto key_blobs = std::make_unique<KeyBlobs>();
  // Note: Yes it is odd to pass the IV from the auth state into the key blobs
  // without performing any operation on the data. However, the fact that the
  // IVs are pre-generated in the VaultKeyset for PinWeaver credentials is an
  // implementation detail. The AuthBlocks are designed to hide those
  // implementation details, so this goes here.
  if (auth_state->chaps_iv.has_value()) {
    key_blobs->chaps_iv = auth_state->chaps_iv.value();
  }
  if (auth_state->fek_iv.has_value()) {
    key_blobs->vkk_iv = auth_state->fek_iv.value();
  }

  // Try to obtain the High Entropy Secret from the PinWeaverManager.
  hwsec::StatusOr<hwsec::PinWeaverManager::CheckCredentialReply> result =
      hwsec_pw_manager_->CheckCredential(auth_state->le_label.value(),
                                         le_secret);

  if (!result.ok()) {
    // If the underlying credential is currently locked, include the
    // kFactorLockedOut action.
    if (GetLockoutDelay(auth_state->le_label.value()) > 0) {
      // If it is caused by invalid LE secret
      if (result.err_status()->ToTPMRetryAction() ==
          hwsec::TPMRetryAction::kUserAuth) {
        std::move(callback).Run(
            MakeStatus<CryptohomeCryptoError>(
                CRYPTOHOME_ERR_LOC(
                    kLocPinWeaverAuthBlockCheckCredLockedInDerive),
                ErrorActionSet(PrimaryAction::kFactorLockedOut),
                CryptoError::CE_CREDENTIAL_LOCKED)
                .Wrap(MakeStatus<CryptohomeTPMError>(
                    std::move(result).err_status())),
            nullptr, std::nullopt);
        return;
        // Or the LE node specified by le_label in PinWeaver is under a lockout
        // timer from previous failed attempts.
      } else if (result.err_status()->ToTPMRetryAction() ==
                 hwsec::TPMRetryAction::kPinWeaverLockedOut) {
        std::move(callback).Run(
            MakeStatus<CryptohomeCryptoError>(
                CRYPTOHOME_ERR_LOC(
                    kLocPinWeaverAuthBlockCheckCredTPMLockedInDerive),
                ErrorActionSet(PrimaryAction::kFactorLockedOut))
                .Wrap(MakeStatus<CryptohomeTPMError>(
                    std::move(result).err_status())),
            nullptr, std::nullopt);
        return;
      }
    }

    std::move(callback).Run(
        MakeStatus<CryptohomeCryptoError>(
            CRYPTOHOME_ERR_LOC(kLocPinWeaverAuthBlockCheckCredFailedInDerive))
            .Wrap(
                MakeStatus<CryptohomeTPMError>(std::move(result).err_status())),
        nullptr, std::nullopt);
    return;
  }
  brillo::SecureBlob& he_secret = result->he_secret;
  key_blobs->reset_secret = result->reset_secret;
  std::optional<AuthBlock::SuggestedAction> suggested_action;

  // If this factor is a PIN factor check if the credential is currently
  // configured to use the modern delay policy. If it is not, attempt to migrate
  // it. If any of that fails we don't fail the already-successful derivation.
  if (*schedule_type == DelayScheduleType::kPin) {
    auto delay_sched =
        hwsec_pw_manager_->GetDelaySchedule(*auth_state->le_label);
    if (delay_sched.ok()) {
      if (*delay_sched != PinDelaySchedule()) {
        LOG(INFO) << "PIN factor is using obsolete delay schedule";
        suggested_action = AuthBlock::SuggestedAction::kRecreate;
      }
    } else {
      LOG(WARNING) << "Unable to determine the PIN delay schedule: "
                   << delay_sched.err_status();
    }
  }

  brillo::SecureBlob vkk_seed =
      HmacSha256(he_secret, brillo::BlobFromString(kHESecretHmacData));
  key_blobs->vkk_key = HmacSha256(kdf_skey, vkk_seed);

  std::move(callback).Run(OkStatus<CryptohomeCryptoError>(),
                          std::move(key_blobs), suggested_action);
}

void PinWeaverAuthBlock::PrepareForRemoval(
    const ObfuscatedUsername& obfuscated_username,
    const AuthBlockState& auth_block_state,
    StatusCallback callback) {
  // Read supported_intents only for AuthFactors with a PinWeaver backend.
  auto* state = std::get_if<PinWeaverAuthBlockState>(&auth_block_state.state);
  if (!state) {
    LOG(ERROR) << "Failed to get AuthBlockState in pinweaver auth block.";
    // This error won't be solved by retrying, go ahead and delete the auth
    // factor anyway.
    std::move(callback).Run(OkStatus<CryptohomeCryptoError>());
    return;
  }

  // Ensure that the AuthFactor has le_label.
  if (!state->le_label.has_value()) {
    LOG(ERROR) << "PinWeaver AuthBlockState does not have le_label.";
    // This error won't be solved by retrying, go ahead and delete the auth
    // factor anyway.
    std::move(callback).Run(OkStatus<CryptohomeCryptoError>());
    return;
  }
  hwsec::Status status =
      hwsec_pw_manager_->RemoveCredential(state->le_label.value());
  if (!status.ok()) {
    // TODO(b/300553666): Don't block the RemovalFactor for other non-retryable
    // libhwsec error actions (kNoRetry).
    if (status->ToTPMRetryAction() == hwsec::TPMRetryAction::kSpaceNotFound) {
      LOG(ERROR) << "Invalid le_label in pinweaver auth block: " << status;
      // This error won't be solved by retrying, go ahead and delete the auth
      // factor anyway.
      std::move(callback).Run(OkStatus<CryptohomeCryptoError>());
      return;
    }
    // Other LE errors might be resolved by retrying, so fail the remove
    // operation here.
    std::move(callback).Run(
        MakeStatus<CryptohomeCryptoError>(
            CRYPTOHOME_ERR_LOC(kLocPinWeaverAuthBlockRemoveCredential),
            ErrorActionSet({PossibleAction::kRetry}))
            .Wrap(MakeStatus<CryptohomeTPMError>(std::move(status))));
    return;
  }
  std::move(callback).Run(OkStatus<CryptohomeCryptoError>());
  return;
}

uint32_t PinWeaverAuthBlock::GetLockoutDelay(uint64_t label) {
  hwsec::StatusOr<uint32_t> delay = hwsec_pw_manager_->GetDelayInSeconds(label);
  if (!delay.ok()) {
    LOG(ERROR)
        << "Failed to obtain the delay in seconds in pinweaver auth block: "
        << std::move(delay).status();
    return 0;
  }

  return delay.value();
}

NonPinweaverPasswordAuthBlock::NonPinweaverPasswordAuthBlock(
    DerivationType derivation_type,
    AsyncInitFeatures& features,
    const hwsec::CryptohomeFrontend& hwsec)
    : AuthBlock(derivation_type), features_(&features), hwsec_(&hwsec) {}

void NonPinweaverPasswordAuthBlock::Derive(
    const AuthInput& auth_input,
    const AuthFactorMetadata& auth_factor_metadata,
    const AuthBlockState& state,
    DeriveCallback callback) {
  DerivePassword(
      auth_input, auth_factor_metadata, state,
      base::BindOnce(
          [](DeriveCallback callback, AsyncInitFeatures* features,
             const hwsec::CryptohomeFrontend* hwsec, CryptohomeStatus error,
             std::unique_ptr<KeyBlobs> key_blobs,
             std::optional<SuggestedAction> suggested_action) {
            // If pinweaver is supported, suggest "recreate" to upgrade the auth
            // block to a pinweaver one. Don't do this if the Derive failed, or
            // if there's already a competing suggested action.
            if (error.ok() && !suggested_action.has_value() &&
                features->IsFeatureEnabled(Features::kPinweaverForPassword) &&
                PinWeaverAuthBlock::IsSupported(*hwsec).ok()) {
              suggested_action = SuggestedAction::kRecreate;
            }
            // Forward the results to the original callback.
            std::move(callback).Run(std::move(error), std::move(key_blobs),
                                    suggested_action);
          },
          std::move(callback), features_, hwsec_));
}

}  // namespace cryptohome
