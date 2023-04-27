// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_factor/types/legacy_fingerprint.h"

#include "cryptohome/auth_factor/auth_factor_label_arity.h"
#include "cryptohome/auth_factor/auth_factor_type.h"
#include "cryptohome/auth_intent.h"

namespace cryptohome {

bool LegacyFingerprintAuthFactorDriver::IsSupported(
    AuthFactorStorageType storage_type,
    const std::set<AuthFactorType>& configured_factors) const {
  return false;
}

bool LegacyFingerprintAuthFactorDriver::IsPrepareRequired() const {
  return true;
}

bool LegacyFingerprintAuthFactorDriver::IsVerifySupported(
    AuthIntent auth_intent) const {
  return auth_intent == AuthIntent::kWebAuthn ||
         auth_intent == AuthIntent::kVerifyOnly;
}

bool LegacyFingerprintAuthFactorDriver::NeedsResetSecret() const {
  return false;
}

bool LegacyFingerprintAuthFactorDriver::NeedsRateLimiter() const {
  return false;
}

AuthFactorLabelArity
LegacyFingerprintAuthFactorDriver::GetAuthFactorLabelArity() const {
  return AuthFactorLabelArity::kNone;
}

std::optional<user_data_auth::AuthFactor>
LegacyFingerprintAuthFactorDriver::TypedConvertToProto(
    const CommonAuthFactorMetadata& common,
    const std::monostate& typed_metadata) const {
  user_data_auth::AuthFactor proto;
  proto.set_type(user_data_auth::AUTH_FACTOR_TYPE_LEGACY_FINGERPRINT);
  return proto;
}

}  // namespace cryptohome
