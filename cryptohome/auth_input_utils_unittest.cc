// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_input_utils.h"

#include <optional>

#include <brillo/secure_blob.h>
#include <cryptohome/proto_bindings/auth_factor.pb.h>
#include <gtest/gtest.h>

#include "cryptohome/key_objects.h"

using brillo::SecureBlob;

namespace cryptohome {

// Test the conversion from the password AuthInput proto into the cryptohome
// struct.
TEST(AuthInputUtils, FromProtoPassword) {
  constexpr char kPassword[] = "fake-password";

  user_data_auth::AuthInput proto;
  proto.mutable_password_input()->set_secret(kPassword);

  std::optional<AuthInput> auth_input = FromProto(proto);
  ASSERT_TRUE(auth_input.has_value());
  EXPECT_EQ(auth_input.value().user_input, SecureBlob(kPassword));
}

// Test the conversion from an empty AuthInput proto fails.
TEST(AuthInputUtils, FromProtoErrorEmpty) {
  std::optional<AuthInput> auth_input = FromProto(user_data_auth::AuthInput());
  EXPECT_FALSE(auth_input.has_value());
}

}  // namespace cryptohome
