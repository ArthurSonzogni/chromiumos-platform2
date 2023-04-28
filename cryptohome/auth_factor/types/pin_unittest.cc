// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_factor/types/pin.h"

#include <memory>
#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libhwsec-foundation/error/testing_helper.h>

#include "cryptohome/auth_factor/auth_factor_type.h"
#include "cryptohome/auth_factor/types/interface.h"
#include "cryptohome/auth_factor/types/test_utils.h"
#include "cryptohome/mock_le_credential_manager.h"

namespace cryptohome {
namespace {

using ::hwsec_foundation::error::testing::ReturnValue;
using ::testing::_;
using ::testing::Eq;
using ::testing::IsFalse;
using ::testing::IsNull;
using ::testing::IsTrue;
using ::testing::Optional;

class PinDriverTest : public AuthFactorDriverGenericTest {
 protected:
  PinDriverTest() {
    auto le_manager = std::make_unique<MockLECredentialManager>();
    le_manager_ = le_manager.get();
    crypto_.set_le_manager_for_testing(std::move(le_manager));
  }

  MockLECredentialManager* le_manager_;
};

TEST_F(PinDriverTest, PinConvertToProto) {
  // Setup
  PinAuthFactorDriver pin_driver(&crypto_);
  AuthFactorDriver& driver = pin_driver;
  AuthFactorMetadata metadata = CreateMetadataWithType<PinAuthFactorMetadata>();
  metadata.common.lockout_policy = LockoutPolicy::kAttemptLimited;

  // Test
  std::optional<user_data_auth::AuthFactor> proto =
      driver.ConvertToProto(kLabel, metadata);

  // Verify
  ASSERT_THAT(proto, Optional(_));
  EXPECT_THAT(proto.value().type(), Eq(user_data_auth::AUTH_FACTOR_TYPE_PIN));
  EXPECT_THAT(proto.value().label(), Eq(kLabel));
  EXPECT_THAT(proto->common_metadata().chromeos_version_last_updated(),
              Eq(kChromeosVersion));
  EXPECT_THAT(proto->common_metadata().chrome_version_last_updated(),
              Eq(kChromeVersion));
  EXPECT_THAT(proto->common_metadata().lockout_policy(),
              Eq(user_data_auth::LOCKOUT_POLICY_ATTEMPT_LIMITED));
  EXPECT_THAT(proto.value().has_pin_metadata(), IsTrue());
}

TEST_F(PinDriverTest, PinConvertToProtoNullOpt) {
  // Setup
  PinAuthFactorDriver pin_driver(&crypto_);
  AuthFactorDriver& driver = pin_driver;
  AuthFactorMetadata metadata;

  // Test
  std::optional<user_data_auth::AuthFactor> proto =
      driver.ConvertToProto(kLabel, metadata);

  // Verify
  EXPECT_THAT(proto, Eq(std::nullopt));
}

TEST_F(PinDriverTest, UnsupportedWithKiosk) {
  // Setup
  PinAuthFactorDriver pin_driver(&crypto_);
  AuthFactorDriver& driver = pin_driver;

  // Test, Verify.
  EXPECT_THAT(driver.IsSupported(AuthFactorStorageType::kUserSecretStash,
                                 {AuthFactorType::kKiosk}),
              IsFalse());
}

TEST_F(PinDriverTest, UnsupportedByBlock) {
  // Setup
  EXPECT_CALL(hwsec_, IsReady()).WillOnce(ReturnValue(false));
  PinAuthFactorDriver pin_driver(&crypto_);
  AuthFactorDriver& driver = pin_driver;

  // Test, Verify
  EXPECT_THAT(driver.IsSupported(AuthFactorStorageType::kUserSecretStash, {}),
              IsFalse());
}

TEST_F(PinDriverTest, SupportedByBlockWithVk) {
  // Setup
  EXPECT_CALL(hwsec_, IsReady()).WillOnce(ReturnValue(true));
  EXPECT_CALL(hwsec_, IsPinWeaverEnabled()).WillOnce(ReturnValue(true));
  PinAuthFactorDriver pin_driver(&crypto_);
  AuthFactorDriver& driver = pin_driver;

  // Test, Verify
  EXPECT_THAT(driver.IsSupported(AuthFactorStorageType::kVaultKeyset, {}),
              IsTrue());
}

TEST_F(PinDriverTest, SupportedByBlockWithUss) {
  // Setup
  EXPECT_CALL(hwsec_, IsReady()).WillOnce(ReturnValue(true));
  EXPECT_CALL(hwsec_, IsPinWeaverEnabled()).WillOnce(ReturnValue(true));
  PinAuthFactorDriver pin_driver(&crypto_);
  AuthFactorDriver& driver = pin_driver;

  // Test, Verify
  EXPECT_THAT(driver.IsSupported(AuthFactorStorageType::kUserSecretStash, {}),
              IsTrue());
}

TEST_F(PinDriverTest, CreateCredentialVerifierFails) {
  PinAuthFactorDriver pin_driver(&crypto_);
  AuthFactorDriver& driver = pin_driver;

  auto verifier = driver.CreateCredentialVerifier(kLabel, {});
  EXPECT_THAT(verifier, IsNull());
}

}  // namespace
}  // namespace cryptohome
