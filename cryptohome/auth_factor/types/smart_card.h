// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_FACTOR_TYPES_SMART_CARD_H_
#define CRYPTOHOME_AUTH_FACTOR_TYPES_SMART_CARD_H_

#include "cryptohome/auth_factor/types/interface.h"

namespace cryptohome {

class SmartCardAuthFactorDriver : public AuthFactorDriver {
 public:
  SmartCardAuthFactorDriver();

  bool NeedsResetSecret() const override;
  bool NeedsRateLimiter() const override;
  AuthFactorLabelArity GetAuthFactorLabelArity() const override;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_FACTOR_TYPES_SMART_CARD_H_
