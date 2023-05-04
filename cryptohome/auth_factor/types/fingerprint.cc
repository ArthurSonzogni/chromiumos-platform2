// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_factor/types/fingerprint.h"

#include <utility>

#include <libhwsec-foundation/status/status_chain.h>

#include "cryptohome/auth_blocks/fingerprint_auth_block.h"
#include "cryptohome/auth_blocks/prepare_token.h"
#include "cryptohome/auth_factor/auth_factor_label_arity.h"
#include "cryptohome/auth_factor/auth_factor_metadata.h"
#include "cryptohome/auth_factor/auth_factor_storage_type.h"
#include "cryptohome/auth_factor/auth_factor_type.h"
#include "cryptohome/error/action.h"
#include "cryptohome/error/location_utils.h"
#include "cryptohome/error/locations.h"
#include "cryptohome/username.h"

namespace cryptohome {
namespace {

using ::cryptohome::error::CryptohomeError;
using ::cryptohome::error::ErrorActionSet;
using ::cryptohome::error::PossibleAction;
using ::hwsec_foundation::status::MakeStatus;

}  // namespace

bool FingerprintAuthFactorDriver::IsSupported(
    const std::set<AuthFactorStorageType>& configured_storage_types,
    const std::set<AuthFactorType>& configured_factors) const {
  if (configured_factors.count(AuthFactorType::kKiosk) > 0) {
    return false;
  }
  return configured_storage_types.size() == 1 &&
         configured_storage_types.count(
             AuthFactorStorageType::kUserSecretStash) &&
         FingerprintAuthBlock::IsSupported(*crypto_, bio_service_).ok();
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

bool FingerprintAuthFactorDriver::NeedsResetSecret() const {
  return false;
}

bool FingerprintAuthFactorDriver::NeedsRateLimiter() const {
  return true;
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
