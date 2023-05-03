// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_factor/types/pin.h"

#include "cryptohome/auth_blocks/pin_weaver_auth_block.h"
#include "cryptohome/auth_factor/auth_factor_label_arity.h"
#include "cryptohome/auth_factor/auth_factor_metadata.h"
#include "cryptohome/auth_factor/auth_factor_type.h"

namespace cryptohome {
namespace {

user_data_auth::LockoutPolicy LockoutPolicyToAuthFactor(
    const std::optional<LockoutPolicy>& policy) {
  if (!policy.has_value()) {
    return user_data_auth::LOCKOUT_POLICY_UNKNOWN;
  }
  switch (policy.value()) {
    case cryptohome::LockoutPolicy::kNoLockout:
      return user_data_auth::LOCKOUT_POLICY_NONE;
    case cryptohome::LockoutPolicy::kAttemptLimited:
      return user_data_auth::LOCKOUT_POLICY_ATTEMPT_LIMITED;
    case cryptohome::LockoutPolicy::kTimeLimited:
      return user_data_auth::LOCKOUT_POLICY_TIME_LIMITED;
  }
}

}  // namespace

bool PinAuthFactorDriver::IsSupported(
    AuthFactorStorageType storage_type,
    const std::set<AuthFactorType>& configured_factors) const {
  if (configured_factors.count(AuthFactorType::kKiosk) > 0) {
    return false;
  }
  return PinWeaverAuthBlock::IsSupported(*crypto_).ok();
}

bool PinAuthFactorDriver::IsPrepareRequired() const {
  return false;
}

bool PinAuthFactorDriver::NeedsResetSecret() const {
  return true;
}

bool PinAuthFactorDriver::NeedsRateLimiter() const {
  return false;
}

AuthFactorLabelArity PinAuthFactorDriver::GetAuthFactorLabelArity() const {
  return AuthFactorLabelArity::kSingle;
}

std::optional<user_data_auth::AuthFactor>
PinAuthFactorDriver::TypedConvertToProto(
    const CommonAuthFactorMetadata& common,
    const PinAuthFactorMetadata& typed_metadata) const {
  user_data_auth::AuthFactor proto;
  proto.set_type(user_data_auth::AUTH_FACTOR_TYPE_PIN);
  proto.mutable_common_metadata()->set_lockout_policy(
      LockoutPolicyToAuthFactor(common.lockout_policy));
  proto.mutable_pin_metadata();
  return proto;
}

}  // namespace cryptohome
