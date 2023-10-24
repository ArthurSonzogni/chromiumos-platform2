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
#include <libhwsec/frontend/cryptohome/frontend.h>
#include <libhwsec-foundation/crypto/secure_blob_util.h>
#include <libhwsec-foundation/status/status_chain.h>
#include <libhwsec-foundation/status/status_chain_macros.h>

#include "cryptohome/auth_blocks/fingerprint_auth_block.h"
#include "cryptohome/auth_blocks/prepare_token.h"
#include "cryptohome/auth_factor/auth_factor_label_arity.h"
#include "cryptohome/auth_factor/auth_factor_metadata.h"
#include "cryptohome/auth_factor/auth_factor_storage_type.h"
#include "cryptohome/auth_factor/auth_factor_type.h"
#include "cryptohome/auth_intent.h"
#include "cryptohome/error/action.h"
#include "cryptohome/error/cryptohome_tpm_error.h"
#include "cryptohome/error/location_utils.h"
#include "cryptohome/error/locations.h"
#include "cryptohome/filesystem_layout.h"
#include "cryptohome/flatbuffer_schemas/auth_factor.h"
#include "cryptohome/user_secret_stash/storage.h"
#include "cryptohome/username.h"

namespace cryptohome {
namespace {

using ::cryptohome::error::CryptohomeError;
using ::cryptohome::error::ErrorActionSet;
using ::cryptohome::error::PossibleAction;
using ::hwsec_foundation::status::MakeStatus;
using ::hwsec_foundation::status::OkStatus;

constexpr uint8_t kFingerprintAuthChannel = 0;
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

bool FingerprintAuthFactorDriver::IsPrepareRequired() const {
  return true;
}

void FingerprintAuthFactorDriver::PrepareForAdd(
    const ObfuscatedUsername& username,
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
  bio_service_->StartEnrollSession(type(), username, std::move(callback));
}

void FingerprintAuthFactorDriver::PrepareForAuthenticate(
    const ObfuscatedUsername& username,
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
  bio_service_->StartAuthenticateSession(type(), username, std::move(callback));
}

bool FingerprintAuthFactorDriver::IsFullAuthSupported(
    AuthIntent auth_intent) const {
  return true;
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
    return MakeStatus<error::CryptohomeTPMError>(
        std::move(result).err_status());
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
  UserUssStorage user_storage(*uss_storage_, username);
  CryptohomeStatusOr<EncryptedUss> uss =
      EncryptedUss::FromStorage(user_storage);
  if (!uss.ok()) {
    return MakeStatus<CryptohomeError>(
               CRYPTOHOME_ERR_LOC(
                   kLocAuthFactorFingerprintGetFactorDelayLoadMetadataFailed))
        .Wrap(std::move(uss).err_status());
  }
  if (!uss->fingerprint_rate_limiter_id()) {
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthFactorFingerprintGetFactorDelayNoLabel),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
  }
  // Try and extract the delay from the LE credential manager.
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

CryptohomeStatusOr<bool> FingerprintAuthFactorDriver::IsExpired(
    const ObfuscatedUsername& username, const AuthFactor& factor) {
  // Do all the error checks to make sure the input is useful.
  if (factor.type() != type()) {
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthFactorFingerprintIsExpiredWrongFactorType),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
  }
  UserUssStorage user_storage(*uss_storage_, username);
  CryptohomeStatusOr<EncryptedUss> uss =
      EncryptedUss::FromStorage(user_storage);
  if (!uss.ok()) {
    return MakeStatus<CryptohomeError>(
               CRYPTOHOME_ERR_LOC(
                   kLocAuthFactorFingerprintIsExpiredLoadMetadataFailed))
        .Wrap(std::move(uss).err_status());
  }
  if (!uss->fingerprint_rate_limiter_id()) {
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthFactorFingerprintIsExpiredNoLabel),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
  }
  // Try and extract the expiration from the LE credential manager.
  hwsec::StatusOr<std::optional<uint32_t>> time_until_expiration_in_seconds =
      crypto_->GetPinWeaverManager()->GetExpirationInSeconds(
          *uss->fingerprint_rate_limiter_id());
  if (!time_until_expiration_in_seconds.ok()) {
    return MakeStatus<CryptohomeError>(
               CRYPTOHOME_ERR_LOC(kLocAuthFactorFingerprintIsExpiredReadFailed))
        .Wrap(MakeStatus<error::CryptohomeTPMError>(
            std::move(time_until_expiration_in_seconds).err_status()));
  }
  // If |time_until_expiration_in_seconds| is nullopt, the leaf has no
  // expiration.
  return time_until_expiration_in_seconds->has_value() &&
         time_until_expiration_in_seconds->value() == 0;
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
  proto.mutable_fingerprint_metadata();
  return proto;
}

}  // namespace cryptohome
