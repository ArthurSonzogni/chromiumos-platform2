// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_factor/types/fingerprint.h"

#include <limits>
#include <utility>

#include <libhwsec-foundation/status/status_chain.h>

#include "cryptohome/auth_blocks/fingerprint_auth_block.h"
#include "cryptohome/auth_blocks/prepare_token.h"
#include "cryptohome/auth_factor/auth_factor_label_arity.h"
#include "cryptohome/auth_factor/auth_factor_metadata.h"
#include "cryptohome/auth_factor/auth_factor_storage_type.h"
#include "cryptohome/auth_factor/auth_factor_type.h"
#include "cryptohome/auth_intent.h"
#include "cryptohome/error/action.h"
#include "cryptohome/error/location_utils.h"
#include "cryptohome/error/locations.h"
#include "cryptohome/filesystem_layout.h"
#include "cryptohome/user_secret_stash/user_metadata.h"
#include "cryptohome/username.h"

namespace cryptohome {
namespace {

using ::cryptohome::error::CryptohomeError;
using ::cryptohome::error::ErrorActionSet;
using ::cryptohome::error::PossibleAction;
using ::hwsec_foundation::status::MakeStatus;

constexpr char kEnableDecryptFilename[] = "fingerprint_decrypt_enable";

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

bool FingerprintAuthFactorDriver::IsFullAuthAllowed(
    AuthIntent auth_intent) const {
  // Support decrypt only if it is explicitly enabled.
  if (auth_intent == AuthIntent::kDecrypt) {
    return DoesFlagFileExist(kEnableDecryptFilename, platform_);
  }
  // All other intents are always supported.
  return true;
}

bool FingerprintAuthFactorDriver::NeedsResetSecret() const {
  return false;
}

bool FingerprintAuthFactorDriver::NeedsRateLimiter() const {
  return true;
}

bool FingerprintAuthFactorDriver::IsDelaySupported() const {
  return true;
}

CryptohomeStatusOr<base::TimeDelta> FingerprintAuthFactorDriver::GetFactorDelay(
    const ObfuscatedUsername& username, const AuthFactor& factor) {
  // Do all the error checks to make sure the input is useful.
  if (factor.type() != type()) {
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(
            kLocAuthFactorFingerprintGetFactorDelayWrongFactorType),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
  }
  if (!user_metadata_reader_) {
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(
            kLocAuthFactorFingerprintGetFactorDelayNoUserMetadataReader),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
  }
  CryptohomeStatusOr<UserMetadata> user_metadata =
      user_metadata_reader_->Load(username);
  if (!user_metadata.ok()) {
    return MakeStatus<CryptohomeError>(
               CRYPTOHOME_ERR_LOC(
                   kLocAuthFactorFingerprintGetFactorDelayLoadMetadataFailed))
        .Wrap(std::move(user_metadata).err_status());
  }
  if (!user_metadata->fingerprint_rate_limiter_id.has_value()) {
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthFactorFingerprintGetFactorDelayNoLabel),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
  }
  // Try and extract the delay from the LE credential manager.
  auto delay_in_seconds = crypto_->le_manager()->GetDelayInSeconds(
      *user_metadata->fingerprint_rate_limiter_id);
  if (!delay_in_seconds.ok()) {
    return MakeStatus<CryptohomeError>(
               CRYPTOHOME_ERR_LOC(
                   kLocAuthFactorFingerprintGetFactorDelayReadFailed))
        .Wrap(std::move(delay_in_seconds).err_status());
  }
  // Return the extracted time, handling the max value case.
  if (*delay_in_seconds == std::numeric_limits<uint32_t>::max()) {
    return base::TimeDelta::Max();
  } else {
    return base::Seconds(*delay_in_seconds);
  }
}

AuthFactorLabelArity FingerprintAuthFactorDriver::GetAuthFactorLabelArity()
    const {
  return AuthFactorLabelArity::kMultiple;
}

std::optional<user_data_auth::AuthFactor>
FingerprintAuthFactorDriver::TypedConvertToProto(
    const CommonAuthFactorMetadata& common,
    const FingerprintAuthFactorMetadata& typed_metadata) const {
  user_data_auth::AuthFactor proto;
  proto.set_type(user_data_auth::AUTH_FACTOR_TYPE_FINGERPRINT);
  proto.mutable_fingerprint_metadata();
  return proto;
}

}  // namespace cryptohome
