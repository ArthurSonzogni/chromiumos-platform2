// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_factor/types/kiosk.h"

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

class KioskDriverTest : public AuthFactorDriverGenericTest {};

TEST_F(KioskDriverTest, KioskConvertToProto) {
  // Setup
  KioskAuthFactorDriver kiosk_driver;
  AuthFactorDriver& driver = kiosk_driver;
  AuthFactorMetadata metadata =
      CreateMetadataWithType<KioskAuthFactorMetadata>();

  // Test
  std::optional<user_data_auth::AuthFactor> proto =
      driver.ConvertToProto(kLabel, metadata);

  // Verify
  ASSERT_THAT(proto, Optional(_));
  EXPECT_THAT(proto.value().type(), Eq(user_data_auth::AUTH_FACTOR_TYPE_KIOSK));
  EXPECT_THAT(proto.value().label(), Eq(kLabel));
  EXPECT_THAT(proto->common_metadata().chromeos_version_last_updated(),
              Eq(kChromeosVersion));
  EXPECT_THAT(proto->common_metadata().chrome_version_last_updated(),
              Eq(kChromeVersion));
  EXPECT_THAT(proto->common_metadata().lockout_policy(),
              Eq(user_data_auth::LOCKOUT_POLICY_NONE));
  EXPECT_THAT(proto.value().has_kiosk_metadata(), IsTrue());
}

TEST_F(KioskDriverTest, KioskConvertToProtoNullOpt) {
  // Setup
  KioskAuthFactorDriver kiosk_driver;
  AuthFactorDriver& driver = kiosk_driver;
  AuthFactorMetadata metadata;

  // Test
  std::optional<user_data_auth::AuthFactor> proto =
      driver.ConvertToProto(kLabel, metadata);

  // Verify
  EXPECT_THAT(proto, Eq(std::nullopt));
}

TEST_F(KioskDriverTest, SupportedWithNoOtherFactors) {
  // Setup
  KioskAuthFactorDriver kiosk_driver;
  AuthFactorDriver& driver = kiosk_driver;

  // Test, Verify
  EXPECT_THAT(driver.IsSupported(AuthFactorStorageType::kVaultKeyset, {}),
              IsTrue());
  EXPECT_THAT(driver.IsSupported(AuthFactorStorageType::kVaultKeyset,
                                 {AuthFactorType::kKiosk}),
              IsTrue());
  EXPECT_THAT(driver.IsSupported(AuthFactorStorageType::kUserSecretStash, {}),
              IsTrue());
  EXPECT_THAT(driver.IsSupported(AuthFactorStorageType::kUserSecretStash,
                                 {AuthFactorType::kKiosk}),
              IsTrue());
}

TEST_F(KioskDriverTest, UnsupportedWithOtherFactors) {
  // Setup
  KioskAuthFactorDriver kiosk_driver;
  AuthFactorDriver& driver = kiosk_driver;

  // Test, Verify
  EXPECT_THAT(driver.IsSupported(AuthFactorStorageType::kVaultKeyset,
                                 {AuthFactorType::kPassword}),
              IsFalse());
  EXPECT_THAT(driver.IsSupported(AuthFactorStorageType::kUserSecretStash,
                                 {AuthFactorType::kPassword}),
              IsFalse());
}

}  // namespace
}  // namespace cryptohome
