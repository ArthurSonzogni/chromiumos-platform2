// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_factor/types/legacy_fingerprint.h"

#include "cryptohome/auth_factor/auth_factor_label_arity.h"
#include "cryptohome/auth_factor/auth_factor_type.h"

namespace cryptohome {

LegacyFingerprintAuthFactorDriver::LegacyFingerprintAuthFactorDriver()
    : AuthFactorDriver(AuthFactorType::kLegacyFingerprint) {}

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

}  // namespace cryptohome
