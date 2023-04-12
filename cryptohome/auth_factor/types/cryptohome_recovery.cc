// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_factor/types/cryptohome_recovery.h"

#include "cryptohome/auth_factor/auth_factor_label_arity.h"
#include "cryptohome/auth_factor/auth_factor_type.h"

namespace cryptohome {

CryptohomeRecoveryAuthFactorDriver::CryptohomeRecoveryAuthFactorDriver()
    : AuthFactorDriver(AuthFactorType::kCryptohomeRecovery) {}

bool CryptohomeRecoveryAuthFactorDriver::NeedsResetSecret() const {
  return false;
}

bool CryptohomeRecoveryAuthFactorDriver::NeedsRateLimiter() const {
  return false;
}

AuthFactorLabelArity
CryptohomeRecoveryAuthFactorDriver::GetAuthFactorLabelArity() const {
  return AuthFactorLabelArity::kSingle;
}

}  // namespace cryptohome
