// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_FACTOR_AUTH_FACTOR_TYPE_H_
#define CRYPTOHOME_AUTH_FACTOR_AUTH_FACTOR_TYPE_H_

namespace cryptohome {

enum class AuthFactorType {
  kPassword,
  // TODO(b:208351356): Add other factor types.
  kUnspecified,
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_FACTOR_AUTH_FACTOR_TYPE_H_
