// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_factor/metadata.h"

#include <variant>

#include <gtest/gtest.h>

namespace cryptohome {

// Make sure that a default-constructed object doesn't have any metadata in it.
TEST(AuthFactorMetadataTest, DefaultConstructor) {
  AuthFactorMetadata metadata;
  EXPECT_FALSE(std::holds_alternative<PasswordMetadata>(metadata.metadata));
}

}  // namespace cryptohome
