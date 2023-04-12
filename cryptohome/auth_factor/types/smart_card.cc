// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_factor/types/smart_card.h"

#include "cryptohome/auth_factor/auth_factor_label_arity.h"
#include "cryptohome/auth_factor/auth_factor_type.h"

namespace cryptohome {

SmartCardAuthFactorDriver::SmartCardAuthFactorDriver()
    : AuthFactorDriver(AuthFactorType::kSmartCard) {}

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

}  // namespace cryptohome
