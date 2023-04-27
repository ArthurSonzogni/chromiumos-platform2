// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_factor/types/smart_card.h"

#include <brillo/secure_blob.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libhwsec-foundation/error/testing_helper.h>

#include "cryptohome/auth_factor/auth_factor_metadata.h"
#include "cryptohome/auth_factor/auth_factor_storage_type.h"
#include "cryptohome/auth_factor/auth_factor_type.h"
#include "cryptohome/auth_factor/types/interface.h"
#include "cryptohome/auth_factor/types/test_utils.h"

namespace cryptohome {
namespace {

using ::hwsec_foundation::error::testing::ReturnValue;
using ::testing::_;
using ::testing::Eq;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::Optional;

class SmartCardDriverTest : public AuthFactorDriverGenericTest {};

TEST_F(SmartCardDriverTest, ConvertToProto) {
  // Setup
  SmartCardAuthFactorDriver sc_driver(&crypto_);
  AuthFactorDriver& driver = sc_driver;
  static constexpr char kPublicKeySpkiDer[] = "abcd1234";
  AuthFactorMetadata metadata =
      CreateMetadataWithType<SmartCardAuthFactorMetadata>(
          {.public_key_spki_der = brillo::BlobFromString(kPublicKeySpkiDer)});

  // Test
  std::optional<user_data_auth::AuthFactor> proto =
      driver.ConvertToProto(kLabel, metadata);

  // Verify
  ASSERT_THAT(proto, Optional(_));
  EXPECT_THAT(proto.value().type(),
              Eq(user_data_auth::AUTH_FACTOR_TYPE_SMART_CARD));
  EXPECT_THAT(proto.value().label(), Eq(kLabel));
  EXPECT_THAT(proto->common_metadata().chromeos_version_last_updated(),
              Eq(kChromeosVersion));
  EXPECT_THAT(proto->common_metadata().chrome_version_last_updated(),
              Eq(kChromeVersion));
  EXPECT_THAT(proto->common_metadata().lockout_policy(),
              Eq(user_data_auth::LOCKOUT_POLICY_NONE));
  EXPECT_THAT(proto.value().has_smart_card_metadata(), IsTrue());
  EXPECT_THAT(proto.value().smart_card_metadata().public_key_spki_der(),
              Eq(kPublicKeySpkiDer));
}

TEST_F(SmartCardDriverTest, ConvertToProtoNullOpt) {
  // Setup
  SmartCardAuthFactorDriver sc_driver(&crypto_);
  AuthFactorDriver& driver = sc_driver;
  AuthFactorMetadata metadata;

  // Test
  std::optional<user_data_auth::AuthFactor> proto =
      driver.ConvertToProto(kLabel, metadata);

  // Verify
  EXPECT_THAT(proto, Eq(std::nullopt));
}

TEST_F(SmartCardDriverTest, UnsupportedWithKiosk) {
  // Setup
  SmartCardAuthFactorDriver sc_driver(&crypto_);
  AuthFactorDriver& driver = sc_driver;

  // Test, Verify.
  EXPECT_THAT(driver.IsSupported(AuthFactorStorageType::kUserSecretStash,
                                 {AuthFactorType::kKiosk}),
              IsFalse());
}

TEST_F(SmartCardDriverTest, UnsupportedByBlock) {
  // Setup
  EXPECT_CALL(hwsec_, IsReady()).WillOnce(ReturnValue(false));
  SmartCardAuthFactorDriver sc_driver(&crypto_);
  AuthFactorDriver& driver = sc_driver;

  // Test, Verify
  EXPECT_THAT(driver.IsSupported(AuthFactorStorageType::kUserSecretStash, {}),
              IsFalse());
}

TEST_F(SmartCardDriverTest, SupportedByBlockWithVk) {
  // Setup
  EXPECT_CALL(hwsec_, IsReady()).WillOnce(ReturnValue(true));
  SmartCardAuthFactorDriver sc_driver(&crypto_);
  AuthFactorDriver& driver = sc_driver;

  // Test, Verify
  EXPECT_THAT(driver.IsSupported(AuthFactorStorageType::kVaultKeyset, {}),
              IsTrue());
}

TEST_F(SmartCardDriverTest, SupportedByBlockWithUss) {
  // Setup
  EXPECT_CALL(hwsec_, IsReady()).WillOnce(ReturnValue(true));
  SmartCardAuthFactorDriver sc_driver(&crypto_);
  AuthFactorDriver& driver = sc_driver;

  // Test, Verify
  EXPECT_THAT(driver.IsSupported(AuthFactorStorageType::kUserSecretStash, {}),
              IsTrue());
}

}  // namespace
}  // namespace cryptohome
