// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_factor/types/smart_card.h"

#include <brillo/secure_blob.h>

#include "cryptohome/auth_blocks/challenge_credential_auth_block.h"
#include "cryptohome/auth_factor/auth_factor_label_arity.h"
#include "cryptohome/auth_factor/auth_factor_metadata.h"
#include "cryptohome/auth_factor/auth_factor_type.h"
#include "cryptohome/auth_intent.h"

namespace cryptohome {

bool SmartCardAuthFactorDriver::IsSupported(
    AuthFactorStorageType storage_type,
    const std::set<AuthFactorType>& configured_factors) const {
  if (configured_factors.count(AuthFactorType::kKiosk) > 0) {
    return false;
  }
  return ChallengeCredentialAuthBlock::IsSupported(*crypto_).ok();
}

bool SmartCardAuthFactorDriver::IsPrepareRequired() const {
  return false;
}

bool SmartCardAuthFactorDriver::IsVerifySupported(
    AuthIntent auth_intent) const {
  return auth_intent == AuthIntent::kVerifyOnly;
}

bool SmartCardAuthFactorDriver::NeedsResetSecret() const {
  return false;
}

bool SmartCardAuthFactorDriver::NeedsRateLimiter() const {
  return false;
}

AuthFactorLabelArity SmartCardAuthFactorDriver::GetAuthFactorLabelArity()
    const {
  return AuthFactorLabelArity::kSingle;
}

std::optional<user_data_auth::AuthFactor>
SmartCardAuthFactorDriver::TypedConvertToProto(
    const CommonAuthFactorMetadata& common,
    const SmartCardAuthFactorMetadata& typed_metadata) const {
  user_data_auth::AuthFactor proto;
  proto.set_type(user_data_auth::AUTH_FACTOR_TYPE_SMART_CARD);
  proto.mutable_smart_card_metadata()->set_public_key_spki_der(
      brillo::BlobToString(*typed_metadata.public_key_spki_der));
  return proto;
}

}  // namespace cryptohome
