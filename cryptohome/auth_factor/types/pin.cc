// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_factor/types/pin.h"

#include "cryptohome/auth_factor/auth_factor_label_arity.h"
#include "cryptohome/auth_factor/auth_factor_type.h"

namespace cryptohome {

PinAuthFactorDriver::PinAuthFactorDriver()
    : AuthFactorDriver(AuthFactorType::kPin) {}

bool PinAuthFactorDriver::NeedsResetSecret() const {
  return true;
}

bool PinAuthFactorDriver::NeedsRateLimiter() const {
  return false;
}

AuthFactorLabelArity PinAuthFactorDriver::GetAuthFactorLabelArity() const {
  return AuthFactorLabelArity::kSingle;
}

}  // namespace cryptohome
