// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_factor/types/cryptohome_recovery.h"

#include <base/test/test_future.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libhwsec-foundation/error/testing_helper.h>

#include "cryptohome/auth_factor/auth_factor_storage_type.h"
#include "cryptohome/auth_factor/auth_factor_type.h"
#include "cryptohome/auth_factor/types/interface.h"
#include "cryptohome/auth_factor/types/test_utils.h"
#include "cryptohome/flatbuffer_schemas/auth_block_state.h"
#include "cryptohome/flatbuffer_schemas/auth_factor.h"

namespace cryptohome {
namespace {

constexpr char kPublicKeyStr[] = "1a2b3c4d5e6f";

using ::base::test::TestFuture;
using ::hwsec_foundation::error::testing::NotOk;
using ::hwsec_foundation::error::testing::ReturnValue;
using ::testing::_;
using ::testing::Eq;
using ::testing::IsFalse;
using ::testing::IsNull;
using ::testing::IsTrue;
using ::testing::Optional;

class CryptohomeRecoveryDriverTest : public AuthFactorDriverGenericTest {};

TEST_F(CryptohomeRecoveryDriverTest, ConvertToProto) {
  // Setup
  CryptohomeRecoveryAuthFactorDriver recovery_driver(&crypto_);
  AuthFactorDriver& driver = recovery_driver;
  AuthFactorMetadata metadata =
      CreateMetadataWithType<CryptohomeRecoveryMetadata>(
          {.mediator_pub_key = brillo::BlobFromString(kPublicKeyStr)});

  // Test
  std::optional<user_data_auth::AuthFactor> proto =
      driver.ConvertToProto(kLabel, metadata);

  // Verify
  ASSERT_THAT(proto, Optional(_));
  EXPECT_THAT(proto.value().type(),
              Eq(user_data_auth::AUTH_FACTOR_TYPE_CRYPTOHOME_RECOVERY));
  EXPECT_THAT(proto.value().label(), Eq(kLabel));
  EXPECT_THAT(proto->common_metadata().chromeos_version_last_updated(),
              Eq(kChromeosVersion));
  EXPECT_THAT(proto->common_metadata().chrome_version_last_updated(),
              Eq(kChromeVersion));
  EXPECT_THAT(proto->common_metadata().lockout_policy(),
              Eq(user_data_auth::LOCKOUT_POLICY_NONE));
  EXPECT_THAT(proto.value().has_cryptohome_recovery_metadata(), IsTrue());
  EXPECT_THAT(proto.value().cryptohome_recovery_metadata().mediator_pub_key(),
              Eq(kPublicKeyStr));
}

TEST_F(CryptohomeRecoveryDriverTest, ConvertToProtoNoMetadata) {
  // Setup
  CryptohomeRecoveryAuthFactorDriver recovery_driver(&crypto_);
  AuthFactorDriver& driver = recovery_driver;
  AuthFactorMetadata metadata =
      CreateMetadataWithType<CryptohomeRecoveryMetadata>();

  // Test
  std::optional<user_data_auth::AuthFactor> proto =
      driver.ConvertToProto(kLabel, metadata);

  // Verify
  ASSERT_THAT(proto, Optional(_));
  EXPECT_THAT(proto.value().type(),
              Eq(user_data_auth::AUTH_FACTOR_TYPE_CRYPTOHOME_RECOVERY));
  EXPECT_THAT(proto.value().label(), Eq(kLabel));
  EXPECT_THAT(proto->common_metadata().chromeos_version_last_updated(),
              Eq(kChromeosVersion));
  EXPECT_THAT(proto->common_metadata().chrome_version_last_updated(),
              Eq(kChromeVersion));
  EXPECT_THAT(proto->common_metadata().lockout_policy(),
              Eq(user_data_auth::LOCKOUT_POLICY_NONE));
  EXPECT_THAT(proto.value().has_cryptohome_recovery_metadata(), IsTrue());
  EXPECT_THAT(proto.value().cryptohome_recovery_metadata().mediator_pub_key(),
              Eq(std::string()));
}

TEST_F(CryptohomeRecoveryDriverTest, ConvertToProtoNullOpt) {
  // Setup
  CryptohomeRecoveryAuthFactorDriver recovery_driver(&crypto_);
  AuthFactorDriver& driver = recovery_driver;
  AuthFactorMetadata metadata;

  // Test
  std::optional<user_data_auth::AuthFactor> proto =
      driver.ConvertToProto(kLabel, metadata);

  // Verify
  EXPECT_THAT(proto, Eq(std::nullopt));
}

TEST_F(CryptohomeRecoveryDriverTest, UnsupportedWithVk) {
  // Setup
  CryptohomeRecoveryAuthFactorDriver recovery_driver(&crypto_);
  AuthFactorDriver& driver = recovery_driver;

  // Test, Verify.
  EXPECT_THAT(
      driver.IsSupportedByStorage({AuthFactorStorageType::kVaultKeyset}, {}),
      IsFalse());
}

TEST_F(CryptohomeRecoveryDriverTest, UnsupportedWithKiosk) {
  // Setup
  CryptohomeRecoveryAuthFactorDriver recovery_driver(&crypto_);
  AuthFactorDriver& driver = recovery_driver;

  // Test, Verify.
  EXPECT_THAT(
      driver.IsSupportedByStorage({AuthFactorStorageType::kUserSecretStash},
                                  {AuthFactorType::kKiosk}),
      IsFalse());
}

TEST_F(CryptohomeRecoveryDriverTest, SupportedWithVkUssMix) {
  // Setup
  CryptohomeRecoveryAuthFactorDriver recovery_driver(&crypto_);
  AuthFactorDriver& driver = recovery_driver;

  // Test, Verify
  EXPECT_THAT(
      driver.IsSupportedByStorage({AuthFactorStorageType::kUserSecretStash,
                                   AuthFactorStorageType::kVaultKeyset},
                                  {}),
      IsTrue());
}

TEST_F(CryptohomeRecoveryDriverTest, UnsupportedByBlock) {
  // Setup
  EXPECT_CALL(hwsec_, IsReady()).WillOnce(ReturnValue(false));
  CryptohomeRecoveryAuthFactorDriver recovery_driver(&crypto_);
  AuthFactorDriver& driver = recovery_driver;

  // Test, Verify
  EXPECT_THAT(driver.IsSupportedByHardware(), IsFalse());
}

TEST_F(CryptohomeRecoveryDriverTest, SupportedByBlock) {
  // Setup
  EXPECT_CALL(hwsec_, IsReady()).WillOnce(ReturnValue(true));
  CryptohomeRecoveryAuthFactorDriver recovery_driver(&crypto_);
  AuthFactorDriver& driver = recovery_driver;

  // Test, Verify
  EXPECT_THAT(driver.IsSupportedByHardware(), IsTrue());
}

TEST_F(CryptohomeRecoveryDriverTest, PrepareForAddFails) {
  CryptohomeRecoveryAuthFactorDriver recovery_driver(&crypto_);
  AuthFactorDriver& driver = recovery_driver;

  TestFuture<CryptohomeStatusOr<std::unique_ptr<PreparedAuthFactorToken>>>
      prepare_result;
  driver.PrepareForAdd(kObfuscatedUser, prepare_result.GetCallback());
  EXPECT_THAT(prepare_result.Get().status()->local_legacy_error(),
              Eq(user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
}

TEST_F(CryptohomeRecoveryDriverTest, PrepareForAuthFails) {
  CryptohomeRecoveryAuthFactorDriver recovery_driver(&crypto_);
  AuthFactorDriver& driver = recovery_driver;

  TestFuture<CryptohomeStatusOr<std::unique_ptr<PreparedAuthFactorToken>>>
      prepare_result;
  driver.PrepareForAuthenticate(kObfuscatedUser, prepare_result.GetCallback());
  EXPECT_THAT(prepare_result.Get().status()->local_legacy_error(),
              Eq(user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
}

TEST_F(CryptohomeRecoveryDriverTest, GetDelayFails) {
  CryptohomeRecoveryAuthFactorDriver recovery_driver(&crypto_);
  AuthFactorDriver& driver = recovery_driver;

  AuthFactor factor(AuthFactorType::kCryptohomeRecovery, kLabel,
                    CreateMetadataWithType<CryptohomeRecoveryMetadata>(),
                    {.state = CryptohomeRecoveryAuthBlockState()});

  auto delay_in_ms = driver.GetFactorDelay(kObfuscatedUser, factor);
  ASSERT_THAT(delay_in_ms, NotOk());
  EXPECT_THAT(delay_in_ms.status()->local_legacy_error(),
              Eq(user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
}

TEST_F(CryptohomeRecoveryDriverTest, GetExpirationFails) {
  CryptohomeRecoveryAuthFactorDriver recovery_driver(&crypto_);
  AuthFactorDriver& driver = recovery_driver;

  AuthFactor factor(AuthFactorType::kCryptohomeRecovery, kLabel,
                    CreateMetadataWithType<CryptohomeRecoveryMetadata>(),
                    {.state = CryptohomeRecoveryAuthBlockState()});

  auto expired = driver.IsExpired(kObfuscatedUser, factor);
  ASSERT_THAT(expired, NotOk());
  EXPECT_THAT(expired.status()->local_legacy_error(),
              Eq(user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
}

TEST_F(CryptohomeRecoveryDriverTest, CreateCredentialVerifierFails) {
  CryptohomeRecoveryAuthFactorDriver recovery_driver(&crypto_);
  AuthFactorDriver& driver = recovery_driver;

  auto verifier = driver.CreateCredentialVerifier(kLabel, {});
  EXPECT_THAT(verifier, IsNull());
}

}  // namespace
}  // namespace cryptohome
