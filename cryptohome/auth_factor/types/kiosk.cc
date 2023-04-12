// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_factor/types/kiosk.h"

#include "cryptohome/auth_factor/auth_factor_label_arity.h"
#include "cryptohome/auth_factor/auth_factor_type.h"

namespace cryptohome {

KioskAuthFactorDriver::KioskAuthFactorDriver()
    : AuthFactorDriver(AuthFactorType::kKiosk) {}

bool KioskAuthFactorDriver::NeedsResetSecret() const {
  return false;
}

bool KioskAuthFactorDriver::NeedsRateLimiter() const {
  return false;
}

AuthFactorLabelArity KioskAuthFactorDriver::GetAuthFactorLabelArity() const {
  return AuthFactorLabelArity::kSingle;
}

}  // namespace cryptohome
