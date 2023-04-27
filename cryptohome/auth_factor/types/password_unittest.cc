// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_factor/types/password.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "cryptohome/auth_factor/auth_factor_type.h"
#include "cryptohome/auth_factor/types/interface.h"
#include "cryptohome/auth_factor/types/test_utils.h"

namespace cryptohome {
namespace {

using ::testing::_;
using ::testing::Eq;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::Optional;

class PasswordDriverTest : public AuthFactorDriverGenericTest {};

TEST_F(PasswordDriverTest, PasswordConvertToProto) {
  // Setup
  PasswordAuthFactorDriver password_driver;
  AuthFactorDriver& driver = password_driver;
  AuthFactorMetadata metadata =
      CreateMetadataWithType<PasswordAuthFactorMetadata>();

  // Test
  std::optional<user_data_auth::AuthFactor> proto =
      driver.ConvertToProto(kLabel, metadata);

  // Verify
  ASSERT_THAT(proto, Optional(_));
  EXPECT_THAT(proto->common_metadata().chromeos_version_last_updated(),
              Eq(kChromeosVersion));
  EXPECT_THAT(proto->common_metadata().chrome_version_last_updated(),
              Eq(kChromeVersion));
  EXPECT_THAT(proto->common_metadata().lockout_policy(),
              Eq(user_data_auth::LOCKOUT_POLICY_NONE));
  EXPECT_THAT(proto.value().type(),
              Eq(user_data_auth::AUTH_FACTOR_TYPE_PASSWORD));
  EXPECT_THAT(proto.value().label(), Eq(kLabel));
  EXPECT_THAT(proto.value().has_password_metadata(), IsTrue());
}

TEST_F(PasswordDriverTest, PasswordConvertToProtoErrorNoMetadata) {
  // Setup
  PasswordAuthFactorDriver password_driver;
  AuthFactorDriver& driver = password_driver;
  AuthFactorMetadata metadata;

  // Test
  std::optional<user_data_auth::AuthFactor> proto =
      driver.ConvertToProto(kLabel, metadata);

  // Verify
  EXPECT_THAT(proto, Eq(std::nullopt));
}

TEST_F(PasswordDriverTest, SupportedWithoutKiosk) {
  // Setup
  PasswordAuthFactorDriver password_driver;
  AuthFactorDriver& driver = password_driver;

  // Test, Verify
  EXPECT_THAT(driver.IsSupported(AuthFactorStorageType::kVaultKeyset, {}),
              IsTrue());
  EXPECT_THAT(driver.IsSupported(AuthFactorStorageType::kVaultKeyset,
                                 {AuthFactorType::kPin}),
              IsTrue());
  EXPECT_THAT(
      driver.IsSupported(AuthFactorStorageType::kVaultKeyset,
                         {AuthFactorType::kPassword, AuthFactorType::kPin}),
      IsTrue());
  EXPECT_THAT(driver.IsSupported(AuthFactorStorageType::kUserSecretStash, {}),
              IsTrue());
  EXPECT_THAT(driver.IsSupported(AuthFactorStorageType::kUserSecretStash,
                                 {AuthFactorType::kPin}),
              IsTrue());
  EXPECT_THAT(
      driver.IsSupported(AuthFactorStorageType::kUserSecretStash,
                         {AuthFactorType::kPassword, AuthFactorType::kPin}),
      IsTrue());
}

TEST_F(PasswordDriverTest, UnsupportedWithKiosk) {
  // Setup
  PasswordAuthFactorDriver password_driver;
  AuthFactorDriver& driver = password_driver;

  // Test, Verify
  EXPECT_THAT(driver.IsSupported(AuthFactorStorageType::kVaultKeyset,
                                 {AuthFactorType::kKiosk}),
              IsFalse());
  EXPECT_THAT(driver.IsSupported(AuthFactorStorageType::kUserSecretStash,
                                 {AuthFactorType::kKiosk}),
              IsFalse());
}

}  // namespace
}  // namespace cryptohome
