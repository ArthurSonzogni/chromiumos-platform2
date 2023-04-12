// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_FACTOR_TYPES_INTERFACE_H_
#define CRYPTOHOME_AUTH_FACTOR_TYPES_INTERFACE_H_

#include "cryptohome/auth_factor/auth_factor_label_arity.h"
#include "cryptohome/auth_factor/auth_factor_type.h"

namespace cryptohome {

// Defines a general interface that implements utility operations for
// interacting with an AuthFactor. This will be subclassed by a separate
// implementation for each AuthFactorType.
class AuthFactorDriver {
 public:
  explicit AuthFactorDriver(AuthFactorType type) : type_(type) {}

  AuthFactorDriver(const AuthFactorDriver&) = delete;
  AuthFactorDriver& operator=(const AuthFactorDriver&) = delete;

  virtual ~AuthFactorDriver() = default;

  AuthFactorType type() const { return type_; }

  // This returns if a type is PinWeaver backed, and thus needs a reset secret.
  virtual bool NeedsResetSecret() const = 0;

  // This returns if a type is PinWeaver rate-limiter backed.
  virtual bool NeedsRateLimiter() const = 0;

  // Return an enum indicating the label arity of the auth factor (e.g. does the
  // factor support single-label authentication or multi-label authentication).
  virtual AuthFactorLabelArity GetAuthFactorLabelArity() const = 0;

 private:
  const AuthFactorType type_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_FACTOR_TYPES_INTERFACE_H_
