// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_FACTOR_AUTH_FACTOR_METADATA_H_
#define CRYPTOHOME_AUTH_FACTOR_AUTH_FACTOR_METADATA_H_

#include <absl/types/variant.h>

namespace cryptohome {

struct PasswordAuthFactorMetadata {};

struct AuthFactorMetadata {
  // Use `absl::monostate` as the first alternative, in order to make the
  // default constructor create an empty metadata.
  absl::variant<absl::monostate, PasswordAuthFactorMetadata> metadata;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_FACTOR_AUTH_FACTOR_METADATA_H_
