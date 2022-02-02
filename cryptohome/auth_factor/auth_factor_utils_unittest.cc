// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <absl/types/variant.h>
#include <gtest/gtest.h>
#include <memory>
#include <string>

#include "cryptohome/auth_factor/auth_factor_metadata.h"
#include "cryptohome/auth_factor/auth_factor_type.h"
#include "cryptohome/auth_factor/auth_factor_utils.h"
namespace cryptohome {

TEST(AuthFactorUtilsTest, AuthFactorMetaDataCheck) {
  // Setup
  constexpr char kLabel[] = "some-label";
  user_data_auth::AuthFactor auth_factor_proto;
  auth_factor_proto.mutable_password_metadata();
  auth_factor_proto.set_type(user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
  auth_factor_proto.set_label(kLabel);

  // Test
  AuthFactorMetadata auth_factor_metadata;
  AuthFactorType auth_factor_type;
  std::string auth_factor_label;
  EXPECT_TRUE(GetAuthFactorMetadata(auth_factor_proto, auth_factor_metadata,
                                    auth_factor_type, auth_factor_label));

  // Verify
  EXPECT_TRUE(absl::holds_alternative<PasswordAuthFactorMetadata>(
      auth_factor_metadata.metadata));
  EXPECT_EQ(auth_factor_type, AuthFactorType::kPassword);
  EXPECT_EQ(auth_factor_label, kLabel);
}

}  // namespace cryptohome
