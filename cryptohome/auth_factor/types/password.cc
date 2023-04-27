// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_factor/types/password.h"

#include "cryptohome/auth_factor/auth_factor_label_arity.h"
#include "cryptohome/auth_factor/auth_factor_metadata.h"
#include "cryptohome/auth_factor/auth_factor_type.h"
#include "cryptohome/auth_intent.h"

namespace cryptohome {

bool PasswordAuthFactorDriver::IsSupported(
    AuthFactorStorageType storage_type,
    const std::set<AuthFactorType>& configured_factors) const {
  if (configured_factors.count(AuthFactorType::kKiosk) > 0) {
    return false;
  }
  return true;
}

bool PasswordAuthFactorDriver::IsPrepareRequired() const {
  return false;
}

bool PasswordAuthFactorDriver::IsVerifySupported(AuthIntent auth_intent) const {
  return auth_intent == AuthIntent::kVerifyOnly;
}

bool PasswordAuthFactorDriver::NeedsResetSecret() const {
  return false;
}

bool PasswordAuthFactorDriver::NeedsRateLimiter() const {
  return false;
}

AuthFactorLabelArity PasswordAuthFactorDriver::GetAuthFactorLabelArity() const {
  return AuthFactorLabelArity::kSingle;
}

std::optional<user_data_auth::AuthFactor>
PasswordAuthFactorDriver::TypedConvertToProto(
    const CommonAuthFactorMetadata& common,
    const PasswordAuthFactorMetadata& typed_metadata) const {
  user_data_auth::AuthFactor proto;
  proto.set_type(user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
  proto.mutable_password_metadata();
  return proto;
}

}  // namespace cryptohome
