// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_factor/types/kiosk.h"

#include "cryptohome/auth_factor/auth_factor_label_arity.h"
#include "cryptohome/auth_factor/auth_factor_metadata.h"
#include "cryptohome/auth_factor/auth_factor_type.h"

namespace cryptohome {

bool KioskAuthFactorDriver::IsSupported(
    AuthFactorStorageType storage_type,
    const std::set<AuthFactorType>& configured_factors) const {
  return configured_factors.empty() ||
         configured_factors.count(AuthFactorType::kKiosk) > 0;
}

bool KioskAuthFactorDriver::IsPrepareRequired() const {
  return false;
}

bool KioskAuthFactorDriver::IsVerifySupported(AuthIntent auth_intent) const {
  return false;
}

std::unique_ptr<CredentialVerifier>
KioskAuthFactorDriver::CreateCredentialVerifier(
    const std::string& auth_factor_label, const AuthInput& auth_input) const {
  return nullptr;
}

bool KioskAuthFactorDriver::NeedsResetSecret() const {
  return false;
}

bool KioskAuthFactorDriver::NeedsRateLimiter() const {
  return false;
}

AuthFactorLabelArity KioskAuthFactorDriver::GetAuthFactorLabelArity() const {
  return AuthFactorLabelArity::kSingle;
}

std::optional<user_data_auth::AuthFactor>
KioskAuthFactorDriver::TypedConvertToProto(
    const CommonAuthFactorMetadata& common,
    const KioskAuthFactorMetadata& typed_metadata) const {
  user_data_auth::AuthFactor proto;
  proto.set_type(user_data_auth::AUTH_FACTOR_TYPE_KIOSK);
  proto.mutable_kiosk_metadata();
  return proto;
}

}  // namespace cryptohome
