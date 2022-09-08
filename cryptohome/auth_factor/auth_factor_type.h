// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_FACTOR_AUTH_FACTOR_TYPE_H_
#define CRYPTOHOME_AUTH_FACTOR_AUTH_FACTOR_TYPE_H_

namespace cryptohome {

enum class AuthFactorType {
  kPassword,
  kPin,
  kCryptohomeRecovery,
  kKiosk,
  kSmartCard,
  kUnspecified,
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_FACTOR_AUTH_FACTOR_TYPE_H_
