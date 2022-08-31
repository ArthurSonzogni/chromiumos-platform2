// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_input_utils.h"

#include <optional>

#include <brillo/secure_blob.h>
#include <cryptohome/proto_bindings/auth_factor.pb.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "cryptohome/filesystem_layout.h"
#include "cryptohome/key_objects.h"
#include "cryptohome/mock_platform.h"

using brillo::SecureBlob;

namespace cryptohome {

namespace {
constexpr char kUserName[] = "someusername";
constexpr char kObfuscatedUsername[] = "fake-user@example.org";

}  // namespace

class AuthInputUtils : public ::testing::Test {
 protected:
  testing::NiceMock<MockPlatform> platform_;
};

// Test the conversion from the password AuthInput proto into the cryptohome
// struct.
TEST_F(AuthInputUtils, CreateAuthInputPassword) {
  constexpr char kPassword[] = "fake-password";

  user_data_auth::AuthInput proto;
  proto.mutable_password_input()->set_secret(kPassword);

  std::optional<AuthInput> auth_input =
      CreateAuthInput(&platform_, proto, kUserName, kObfuscatedUsername,
                      /*locked_to_single_user=*/false,
                      /*cryptohome_recovery_ephemeral_pub_key=*/std::nullopt);
  ASSERT_TRUE(auth_input.has_value());
  EXPECT_EQ(auth_input.value().user_input, SecureBlob(kPassword));
  EXPECT_EQ(auth_input.value().obfuscated_username, kObfuscatedUsername);
  EXPECT_EQ(auth_input.value().locked_to_single_user, false);
}

// Test the conversion from the password AuthInput proto into the cryptohome
// struct, with the locked_to_single_user flag set.
TEST_F(AuthInputUtils, CreateAuthInputPasswordLocked) {
  constexpr char kPassword[] = "fake-password";

  user_data_auth::AuthInput proto;
  proto.mutable_password_input()->set_secret(kPassword);

  std::optional<AuthInput> auth_input =
      CreateAuthInput(&platform_, proto, kUserName, kObfuscatedUsername,
                      /*locked_to_single_user=*/true,
                      /*cryptohome_recovery_ephemeral_pub_key=*/std::nullopt);
  ASSERT_TRUE(auth_input.has_value());
  EXPECT_EQ(auth_input.value().user_input, SecureBlob(kPassword));
  EXPECT_EQ(auth_input.value().obfuscated_username, kObfuscatedUsername);
  EXPECT_EQ(auth_input.value().locked_to_single_user, true);
}

// Test the conversion from an empty AuthInput proto fails.
TEST_F(AuthInputUtils, CreateAuthInputErrorEmpty) {
  user_data_auth::AuthInput proto;

  std::optional<AuthInput> auth_input =
      CreateAuthInput(&platform_, proto, kUserName, kObfuscatedUsername,
                      /*locked_to_single_user=*/false,
                      /*cryptohome_recovery_ephemeral_pub_key=*/std::nullopt);
  EXPECT_FALSE(auth_input.has_value());
}

TEST_F(AuthInputUtils, CreateAuthInputRecoveryCreate) {
  constexpr char kMediatorPubKey[] = "fake_mediator_pub_key";

  user_data_auth::AuthInput proto;
  proto.mutable_cryptohome_recovery_input()->set_mediator_pub_key(
      kMediatorPubKey);

  std::optional<AuthInput> auth_input =
      CreateAuthInput(&platform_, proto, kUserName, kObfuscatedUsername,
                      /*locked_to_single_user=*/true,
                      /*cryptohome_recovery_ephemeral_pub_key=*/std::nullopt);
  ASSERT_TRUE(auth_input.has_value());
  ASSERT_TRUE(auth_input.value().cryptohome_recovery_auth_input.has_value());
  EXPECT_EQ(auth_input.value()
                .cryptohome_recovery_auth_input.value()
                .mediator_pub_key,
            SecureBlob(kMediatorPubKey));
}

TEST_F(AuthInputUtils, CreateAuthInputRecoveryDerive) {
  constexpr char kEpochResponse[] = "fake_epoch_response";
  constexpr char kResponsePayload[] = "fake_recovery_response";
  SecureBlob ephemeral_pub_key = SecureBlob("fake_ephemeral_pub_key");

  user_data_auth::AuthInput proto;
  proto.mutable_cryptohome_recovery_input()->set_epoch_response(kEpochResponse);
  proto.mutable_cryptohome_recovery_input()->set_recovery_response(
      kResponsePayload);

  std::optional<AuthInput> auth_input =
      CreateAuthInput(&platform_, proto, kUserName, kObfuscatedUsername,
                      /*locked_to_single_user=*/true, ephemeral_pub_key);
  ASSERT_TRUE(auth_input.has_value());
  ASSERT_TRUE(auth_input.value().cryptohome_recovery_auth_input.has_value());
  EXPECT_EQ(
      auth_input.value().cryptohome_recovery_auth_input.value().epoch_response,
      SecureBlob(kEpochResponse));
  EXPECT_EQ(auth_input.value()
                .cryptohome_recovery_auth_input.value()
                .recovery_response,
            SecureBlob(kResponsePayload));
  EXPECT_EQ(auth_input.value()
                .cryptohome_recovery_auth_input.value()
                .ephemeral_pub_key,
            ephemeral_pub_key);
}

TEST_F(AuthInputUtils, FromKioskAuthInput) {
  // SETUP
  testing::NiceMock<MockPlatform> platform;
  // Generate a valid passkey from the users id and public salt.
  brillo::SecureBlob public_mount_salt;
  // Mock platform takes care of creating the salt file if needed.
  GetPublicMountSalt(&platform, &public_mount_salt);
  brillo::SecureBlob passkey;
  Crypto::PasswordToPasskey(kUserName, public_mount_salt, &passkey);
  user_data_auth::AuthInput proto;
  proto.mutable_kiosk_input();

  std::optional<AuthInput> auth_input =
      CreateAuthInput(&platform, proto, kUserName, kObfuscatedUsername,
                      /*locked_to_single_user=*/true,
                      /*cryptohome_recovery_ephemeral_pub_key=*/std::nullopt);
  ASSERT_TRUE(auth_input.has_value());

  // TEST
  EXPECT_EQ(auth_input->user_input, passkey);
}

TEST_F(AuthInputUtils, FromKioskAuthInputFail) {
  // SETUP
  EXPECT_CALL(platform_,
              WriteSecureBlobToFileAtomicDurable(PublicMountSaltFile(), _, _))
      .WillOnce(Return(false));
  user_data_auth::AuthInput proto;
  proto.mutable_kiosk_input();

  std::optional<AuthInput> auth_input =
      CreateAuthInput(&platform_, proto, kUserName, kObfuscatedUsername,
                      /*locked_to_single_user=*/true,
                      /*cryptohome_recovery_ephemeral_pub_key=*/std::nullopt);
  ASSERT_FALSE(auth_input.has_value());
}

}  // namespace cryptohome
