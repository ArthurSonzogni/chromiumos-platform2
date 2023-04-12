// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_factor/types/password.h"

#include "cryptohome/auth_factor/auth_factor_label_arity.h"
#include "cryptohome/auth_factor/auth_factor_type.h"

namespace cryptohome {

PasswordAuthFactorDriver::PasswordAuthFactorDriver()
    : AuthFactorDriver(AuthFactorType::kPassword) {}

bool PasswordAuthFactorDriver::NeedsResetSecret() const {
  return false;
}

bool PasswordAuthFactorDriver::NeedsRateLimiter() const {
  return false;
}

AuthFactorLabelArity PasswordAuthFactorDriver::GetAuthFactorLabelArity() const {
  return AuthFactorLabelArity::kSingle;
}

}  // namespace cryptohome
