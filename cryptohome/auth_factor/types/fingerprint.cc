// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_factor/types/fingerprint.h"

#include "cryptohome/auth_factor/auth_factor_label_arity.h"
#include "cryptohome/auth_factor/auth_factor_metadata.h"
#include "cryptohome/auth_factor/auth_factor_type.h"

namespace cryptohome {

FingerprintAuthFactorDriver::FingerprintAuthFactorDriver()
    : TypedAuthFactorDriver(AuthFactorType::kFingerprint) {}

bool FingerprintAuthFactorDriver::IsPrepareRequired() const {
  return true;
}

bool FingerprintAuthFactorDriver::IsVerifySupported(
    AuthIntent auth_intent) const {
  return false;
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
  return proto;
}

}  // namespace cryptohome
