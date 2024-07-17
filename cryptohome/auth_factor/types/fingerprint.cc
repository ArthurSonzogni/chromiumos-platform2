// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_factor/types/fingerprint.h"

#include <cstdint>
#include <limits>
#include <map>
#include <utility>
#include <vector>

#include <base/time/time.h>
#include <libhwsec-foundation/crypto/secure_blob_util.h>
#include <libhwsec-foundation/status/status_chain.h>
#include <libhwsec-foundation/status/status_chain_macros.h>
#include <libhwsec/frontend/cryptohome/frontend.h>

#include "cryptohome/auth_blocks/biometrics_auth_block_service.h"
#include "cryptohome/auth_blocks/fingerprint_auth_block.h"
#include "cryptohome/auth_blocks/prepare_token.h"
#include "cryptohome/auth_factor/label_arity.h"
#include "cryptohome/error/action.h"
#include "cryptohome/error/cryptohome_error.h"
#include "cryptohome/error/cryptohome_tpm_error.h"
#include "cryptohome/error/locations.h"
#include "cryptohome/flatbuffer_schemas/auth_factor.h"
#include "cryptohome/username.h"

namespace cryptohome {
namespace {

using ::cryptohome::error::CryptohomeCryptoError;
using ::cryptohome::error::CryptohomeError;
using ::cryptohome::error::CryptohomeTPMError;
using ::cryptohome::error::ErrorActionSet;
using ::cryptohome::error::PossibleAction;
using ::hwsec::PinWeaverManagerFrontend;
using ::hwsec::PinWeaverManagerFrontend::AuthChannel::kFingerprintAuthChannel;
using ::hwsec_foundation::status::MakeStatus;
using ::hwsec_foundation::status::OkStatus;

constexpr uint32_t kInfiniteDelay = std::numeric_limits<uint32_t>::max();
constexpr size_t kResetSecretSize = 32;

constexpr struct {
  uint32_t attempts;
  uint32_t delay;
} kDefaultDelaySchedule[] = {
    {5, kInfiniteDelay},
};
constexpr base::TimeDelta kExpirationLockout = base::Days(1);

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

bool FingerprintAuthFactorDriver::IsSupportedByHardware() const {
  return FingerprintAuthBlock::IsSupported(*crypto_, bio_service_).ok();
}

AuthFactorDriver::PrepareRequirement
FingerprintAuthFactorDriver::GetPrepareRequirement(
    AuthFactorPreparePurpose purpose) const {
  switch (purpose) {
    case AuthFactorPreparePurpose::kPrepareAddAuthFactor:
      return PrepareRequirement::kOnce;
    case AuthFactorPreparePurpose::kPrepareAuthenticateAuthFactor:
      return PrepareRequirement::kEach;
  }
}

void FingerprintAuthFactorDriver::PrepareForAdd(
    const PrepareInput& prepare_input,
    PreparedAuthFactorToken::Consumer callback) {
  if (!bio_service_) {
    std::move(callback).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthFactorFpPrepareForAddNoService),
        ErrorActionSet(
            {PossibleAction::kDevCheckUnexpectedState, PossibleAction::kAuth}),
        user_data_auth::CryptohomeErrorCode::
            CRYPTOHOME_ERROR_INVALID_ARGUMENT));
    return;
  }

  // reset_secret here represents the existing/created rate limiter leaf's reset
  // secret. The same value will be used as the reset secret for the actual
  // fingerprint credential leaf. It usually never needs to be reset as
  // its authentication shouldn't never fail, but we still need to be able to
  // reset it when it's locked.
  if (!prepare_input.rate_limiter_label.has_value() ||
      !prepare_input.reset_secret.has_value()) {
    std::move(callback).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthFactorFpNoResetSecretInPrepareAdd),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
    return;
  }

  bio_service_->GetNonce(base::BindOnce(
      &FingerprintAuthFactorDriver::PrepareForAddOnGetNonce,
      weak_factory_.GetWeakPtr(), std::move(callback), prepare_input));
}

void FingerprintAuthFactorDriver::PrepareForAddOnGetNonce(
    PreparedAuthFactorToken::Consumer callback,
    const PrepareInput& prepare_input,
    std::optional<brillo::Blob> nonce) {
  CHECK(bio_service_);
  CHECK(prepare_input.rate_limiter_label.has_value());
  CHECK(prepare_input.reset_secret.has_value());

  if (!nonce.has_value()) {
    std::move(callback).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthFactorFpPrepareAddGetNonceFailed),
        ErrorActionSet({PossibleAction::kReboot}),
        user_data_auth::CRYPTOHOME_ERROR_FINGERPRINT_ERROR_INTERNAL));
    return;
  }

  hwsec::StatusOr<PinWeaverManagerFrontend::StartBiometricsAuthReply> reply =
      crypto_->GetPinWeaverManager()->StartBiometricsAuth(
          kFingerprintAuthChannel, *prepare_input.rate_limiter_label,
          std::move(*nonce));
  if (!reply.ok()) {
    std::move(callback).Run(
        MakeStatus<CryptohomeCryptoError>(
            CRYPTOHOME_ERR_LOC(kLocAuthFactorFpPrepareAddStartBioAuthFailed))
            .Wrap(
                MakeStatus<CryptohomeTPMError>(std::move(reply).err_status())));
    return;
  }
  hwsec::Status reset_status = crypto_->GetPinWeaverManager()->ResetCredential(
      *prepare_input.rate_limiter_label, *prepare_input.reset_secret,
      PinWeaverManagerFrontend::ResetType::kWrongAttempts);
  if (!reset_status.ok()) {
    // TODO(b/275027852): Report metrics because we silently fail here.
    LOG(WARNING) << "Failed to reset rate-limiter during PrepareForAdd. This "
                    "doesn't block the creation but shouldn't normally happen.";
  }
  BiometricsAuthBlockService::OperationInput input{
      .nonce = std::move(reply->server_nonce),
      .encrypted_label_seed = std::move(reply->encrypted_he_secret),
      .iv = std::move(reply->iv),
  };
  bio_service_->StartEnrollSession(type(), std::move(input),
                                   std::move(callback));
}

void FingerprintAuthFactorDriver::PrepareForAuthenticate(
    const PrepareInput& prepare_input,
    PreparedAuthFactorToken::Consumer callback) {
  if (!bio_service_) {
    std::move(callback).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthFactorFpPrepareForAuthNoService),
        ErrorActionSet(
            {PossibleAction::kDevCheckUnexpectedState, PossibleAction::kAuth}),
        user_data_auth::CryptohomeErrorCode::
            CRYPTOHOME_ERROR_INVALID_ARGUMENT));
    return;
  }

  if (!prepare_input.rate_limiter_label.has_value()) {
    std::move(callback).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthFactorFpNoResetSecretInPrepareAuth),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
    return;
  }

  bio_service_->GetNonce(base::BindOnce(
      &FingerprintAuthFactorDriver::PrepareForAuthOnGetNonce,
      weak_factory_.GetWeakPtr(), std::move(callback), prepare_input));
}

void FingerprintAuthFactorDriver::PrepareForAuthOnGetNonce(
    PreparedAuthFactorToken::Consumer callback,
    const PrepareInput& prepare_input,
    std::optional<brillo::Blob> nonce) {
  CHECK(bio_service_);
  CHECK(prepare_input.rate_limiter_label.has_value());

  if (!nonce.has_value()) {
    std::move(callback).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthFactorFpPrepareAuthGetNonceFailed),
        ErrorActionSet({PossibleAction::kReboot}),
        user_data_auth::CRYPTOHOME_ERROR_FINGERPRINT_ERROR_INTERNAL));
    return;
  }

  hwsec::StatusOr<PinWeaverManagerFrontend::StartBiometricsAuthReply> reply =
      crypto_->GetPinWeaverManager()->StartBiometricsAuth(
          kFingerprintAuthChannel, *prepare_input.rate_limiter_label,
          std::move(*nonce));
  if (!reply.ok()) {
    std::move(callback).Run(
        MakeStatus<CryptohomeCryptoError>(
            CRYPTOHOME_ERR_LOC(kLocAuthFactorFpPrepareAuthStartBioAuthFailed))
            .Wrap(
                MakeStatus<CryptohomeTPMError>(std::move(reply).err_status())));
    return;
  }
  BiometricsAuthBlockService::OperationInput input{
      .nonce = std::move(reply->server_nonce),
      .encrypted_label_seed = std::move(reply->encrypted_he_secret),
      .iv = std::move(reply->iv),
  };
  bio_service_->StartAuthenticateSession(type(), prepare_input.username,
                                         std::move(input), std::move(callback));
}

bool FingerprintAuthFactorDriver::NeedsResetSecret() const {
  return false;
}

bool FingerprintAuthFactorDriver::NeedsRateLimiter() const {
  return true;
}

CryptohomeStatus FingerprintAuthFactorDriver::TryCreateRateLimiter(
    const ObfuscatedUsername& username, DecryptedUss& decrypted_uss) {
  std::optional<uint64_t> rate_limiter_label =
      decrypted_uss.encrypted().fingerprint_rate_limiter_id();
  if (rate_limiter_label.has_value()) {
    return OkStatus<CryptohomeError>();
  }
  auto reset_secret =
      hwsec_foundation::CreateSecureRandomBlob(kResetSecretSize);
  std::vector<hwsec::OperationPolicySetting> policies =
      GetValidPoliciesOfUser(username);

  std::map<uint32_t, uint32_t> delay_sched;
  for (const auto& entry : kDefaultDelaySchedule) {
    delay_sched[entry.attempts] = entry.delay;
  }

  hwsec::StatusOr<uint64_t> result =
      crypto_->GetPinWeaverManager()->InsertRateLimiter(
          kFingerprintAuthChannel, policies, reset_secret, delay_sched,
          kExpirationLockout.InSeconds());
  if (!result.ok()) {
    return MakeStatus<CryptohomeCryptoError>(
               CRYPTOHOME_ERR_LOC(
                   kLocAuthFactorFpTryCreateRateLimiterInsertFailed))
        .Wrap(MakeStatus<error::CryptohomeTPMError>(
            std::move(result).err_status()));
  }
  uint64_t& label = result.value();

  // Attempt to populate the USS with the values.
  {
    auto transaction = decrypted_uss.StartTransaction();
    RETURN_IF_ERROR(transaction.InitializeFingerprintRateLimiterId(label));
    RETURN_IF_ERROR(transaction.InsertRateLimiterResetSecret(
        type(), std::move(reset_secret)));
    RETURN_IF_ERROR(std::move(transaction).Commit());
  }
  return OkStatus<CryptohomeError>();
}

bool FingerprintAuthFactorDriver::IsDelaySupported() const {
  return true;
}

CryptohomeStatusOr<base::TimeDelta> FingerprintAuthFactorDriver::GetFactorDelay(
    const ObfuscatedUsername& username, const AuthFactor& factor) const {
  // Do all the error checks to make sure the input is useful.
  if (factor.type() != type()) {
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(
            kLocAuthFactorFingerprintGetFactorDelayWrongFactorType),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
  }
  ASSIGN_OR_RETURN(
      const EncryptedUss* uss, uss_manager_->LoadEncrypted(username),
      _.WithStatus<CryptohomeError>(CRYPTOHOME_ERR_LOC(
          kLocAuthFactorFingerprintGetFactorDelayLoadMetadataFailed)));
  if (!uss->fingerprint_rate_limiter_id()) {
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthFactorFingerprintGetFactorDelayNoLabel),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
  }
  // Try and extract the delay from pinweaver manager..
  auto delay_in_seconds = crypto_->GetPinWeaverManager()->GetDelayInSeconds(
      *uss->fingerprint_rate_limiter_id());
  if (!delay_in_seconds.ok()) {
    return MakeStatus<CryptohomeError>(
               CRYPTOHOME_ERR_LOC(
                   kLocAuthFactorFingerprintGetFactorDelayReadFailed))
        .Wrap(MakeStatus<error::CryptohomeTPMError>(
            std::move(delay_in_seconds).err_status()));
  }
  // Return the extracted time, handling the max value case.
  if (*delay_in_seconds == std::numeric_limits<uint32_t>::max()) {
    return base::TimeDelta::Max();
  } else {
    return base::Seconds(*delay_in_seconds);
  }
}

bool FingerprintAuthFactorDriver::IsExpirationSupported() const {
  return true;
}

CryptohomeStatusOr<base::TimeDelta>
FingerprintAuthFactorDriver::GetTimeUntilExpiration(
    const ObfuscatedUsername& username, const AuthFactor& factor) const {
  // Do all the error checks to make sure the input is useful.
  if (factor.type() != type()) {
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthFactorFingerprintIsExpiredWrongFactorType),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
  }
  ASSIGN_OR_RETURN(const EncryptedUss* uss,
                   uss_manager_->LoadEncrypted(username),
                   _.WithStatus<CryptohomeError>(CRYPTOHOME_ERR_LOC(
                       kLocAuthFactorFingerprintIsExpiredLoadMetadataFailed)));
  if (!uss->fingerprint_rate_limiter_id()) {
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthFactorFingerprintIsExpiredNoLabel),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
  }
  // Try and extract the expiration from pinweaver manager.
  hwsec::StatusOr<std::optional<uint32_t>> time_until_expiration_in_seconds =
      crypto_->GetPinWeaverManager()->GetExpirationInSeconds(
          *uss->fingerprint_rate_limiter_id());
  if (!time_until_expiration_in_seconds.ok()) {
    return MakeStatus<CryptohomeError>(
               CRYPTOHOME_ERR_LOC(kLocAuthFactorFingerprintIsExpiredReadFailed))
        .Wrap(MakeStatus<error::CryptohomeTPMError>(
            std::move(time_until_expiration_in_seconds).err_status()));
  }
  // Currently fingerprint auth factors should always have finite expiraition.
  if (!time_until_expiration_in_seconds->has_value()) {
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthFactorFingerprintIsExpiredNoExpiration),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }
  return base::Seconds(time_until_expiration_in_seconds->value());
}

AuthFactorLabelArity FingerprintAuthFactorDriver::GetAuthFactorLabelArity()
    const {
  return AuthFactorLabelArity::kMultiple;
}

std::optional<user_data_auth::AuthFactor>
FingerprintAuthFactorDriver::TypedConvertToProto(
    const CommonMetadata& common,
    const FingerprintMetadata& typed_metadata) const {
  user_data_auth::AuthFactor proto;
  proto.set_type(user_data_auth::AUTH_FACTOR_TYPE_FINGERPRINT);
  user_data_auth::FingerprintMetadata& fingerprint_metadata =
      *proto.mutable_fingerprint_metadata();
  if (typed_metadata.was_migrated.has_value()) {
    fingerprint_metadata.set_was_migrated(*typed_metadata.was_migrated);
  }
  return proto;
}

}  // namespace cryptohome
