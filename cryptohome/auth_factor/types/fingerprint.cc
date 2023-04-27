// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_factor/types/fingerprint.h"

#include "cryptohome/auth_blocks/fingerprint_auth_block.h"
#include "cryptohome/auth_factor/auth_factor_label_arity.h"
#include "cryptohome/auth_factor/auth_factor_metadata.h"
#include "cryptohome/auth_factor/auth_factor_storage_type.h"
#include "cryptohome/auth_factor/auth_factor_type.h"

namespace cryptohome {

bool FingerprintAuthFactorDriver::IsSupported(
    AuthFactorStorageType storage_type,
    const std::set<AuthFactorType>& configured_factors) const {
  if (configured_factors.count(AuthFactorType::kKiosk) > 0) {
    return false;
  }
  return storage_type == AuthFactorStorageType::kUserSecretStash &&
         FingerprintAuthBlock::IsSupported(*crypto_, bio_service_).ok();
}

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
  proto.mutable_fingerprint_metadata();
  return proto;
}

}  // namespace cryptohome
