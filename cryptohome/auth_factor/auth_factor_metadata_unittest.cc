// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <absl/types/variant.h>

#include <gtest/gtest.h>

#include "cryptohome/auth_factor/auth_factor_metadata.h"

namespace cryptohome {

// Make sure that a default-constructed object doesn't have any metadata in it.
TEST(AuthFactorMetadataTest, DefaultConstructor) {
  AuthFactorMetadata metadata;
  EXPECT_FALSE(
      absl::holds_alternative<PasswordAuthFactorMetadata>(metadata.metadata));
}

}  // namespace cryptohome
