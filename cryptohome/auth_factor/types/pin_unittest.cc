// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_factor/types/pin.h"

#include <limits>
#include <memory>
#include <string>

#include <base/test/test_future.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libhwsec-foundation/error/testing_helper.h>

#include "base/time/time.h"
#include "cryptohome/auth_factor/metadata.h"
#include "cryptohome/auth_factor/type.h"
#include "cryptohome/auth_factor/types/interface.h"
#include "cryptohome/auth_factor/types/test_utils.h"
#include "cryptohome/flatbuffer_schemas/auth_block_state.h"
#include "cryptohome/flatbuffer_schemas/auth_factor.h"

namespace cryptohome {
namespace {

using ::base::test::TestFuture;
using ::hwsec_foundation::error::testing::IsOk;
using ::hwsec_foundation::error::testing::NotOk;
using ::hwsec_foundation::error::testing::ReturnValue;
using ::testing::_;
using ::testing::Eq;
using ::testing::IsFalse;
using ::testing::IsNull;
using ::testing::IsTrue;
using ::testing::Optional;

class PinDriverTest : public AuthFactorDriverGenericTest {
 protected:
  static constexpr uint64_t kLeLabel = 0xdeadbeefbaadf00d;

  PinDriverTest() = default;
};

TEST_F(PinDriverTest, PinConvertToProto) {
  // Setup
  const std::string kSalt = "fake_salt";
  PinAuthFactorDriver pin_driver(&crypto_);
  AuthFactorDriver& driver = pin_driver;
  AuthFactorMetadata metadata = CreateMetadataWithType<PinMetadata>(
      {.hash_info = SerializedKnowledgeFactorHashInfo{
           .algorithm =
               SerializedKnowledgeFactorHashAlgorithm::PBKDF2_AES256_1234,
           .salt = brillo::BlobFromString(kSalt),
           .should_generate_key_store = true,
       }});
  metadata.common.lockout_policy = SerializedLockoutPolicy::ATTEMPT_LIMITED;

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
  ASSERT_THAT(proto.value().has_pin_metadata(), IsTrue());
  EXPECT_EQ(proto->pin_metadata().hash_info().algorithm(),
            KnowledgeFactorHashAlgorithm::HASH_TYPE_PBKDF2_AES256_1234);
  EXPECT_EQ(proto->pin_metadata().hash_info().salt(), kSalt);
  EXPECT_TRUE(proto->pin_metadata().hash_info().should_generate_key_store());
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
  EXPECT_THAT(
      driver.IsSupportedByStorage({AuthFactorStorageType::kUserSecretStash},
                                  {AuthFactorType::kKiosk}),
      IsFalse());
}

TEST_F(PinDriverTest, SupportedWithVk) {
  // Setup
  PinAuthFactorDriver pin_driver(&crypto_);
  AuthFactorDriver& driver = pin_driver;

  // Test, Verify
  EXPECT_THAT(
      driver.IsSupportedByStorage({AuthFactorStorageType::kVaultKeyset}, {}),
      IsTrue());
}

TEST_F(PinDriverTest, SupportedWithUss) {
  // Setup
  PinAuthFactorDriver pin_driver(&crypto_);
  AuthFactorDriver& driver = pin_driver;

  // Test, Verify
  EXPECT_THAT(driver.IsSupportedByStorage(
                  {AuthFactorStorageType::kUserSecretStash}, {}),
              IsTrue());
}

TEST_F(PinDriverTest, UnsupportedByBlock) {
  // Setup
  EXPECT_CALL(hwsec_, IsReady()).WillOnce(ReturnValue(false));
  PinAuthFactorDriver pin_driver(&crypto_);
  AuthFactorDriver& driver = pin_driver;

  // Test, Verify
  EXPECT_THAT(driver.IsSupportedByHardware(), IsFalse());
}

TEST_F(PinDriverTest, SupportedByBlock) {
  // Setup
  EXPECT_CALL(hwsec_, IsReady()).WillOnce(ReturnValue(true));
  EXPECT_CALL(hwsec_, IsPinWeaverEnabled()).WillOnce(ReturnValue(true));
  PinAuthFactorDriver pin_driver(&crypto_);
  AuthFactorDriver& driver = pin_driver;

  // Test, Verify
  EXPECT_THAT(driver.IsSupportedByHardware(), IsTrue());
}

TEST_F(PinDriverTest, PrepareForAddFails) {
  PinAuthFactorDriver pin_driver(&crypto_);
  AuthFactorDriver& driver = pin_driver;

  TestFuture<CryptohomeStatusOr<std::unique_ptr<PreparedAuthFactorToken>>>
      prepare_result;
  PrepareInput prepare_input{.username = kObfuscatedUser};
  driver.PrepareForAdd(prepare_input, prepare_result.GetCallback());
  EXPECT_THAT(prepare_result.Get().status()->local_legacy_error(),
              Eq(user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
}

TEST_F(PinDriverTest, PrepareForAuthFails) {
  PinAuthFactorDriver pin_driver(&crypto_);
  AuthFactorDriver& driver = pin_driver;

  TestFuture<CryptohomeStatusOr<std::unique_ptr<PreparedAuthFactorToken>>>
      prepare_result;
  PrepareInput prepare_input{.username = kObfuscatedUser};
  driver.PrepareForAuthenticate(prepare_input, prepare_result.GetCallback());
  EXPECT_THAT(prepare_result.Get().status()->local_legacy_error(),
              Eq(user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
}

TEST_F(PinDriverTest, GetDelayFailsWithWrongFactorType) {
  PinAuthFactorDriver pin_driver(&crypto_);
  AuthFactorDriver& driver = pin_driver;

  AuthFactor factor(AuthFactorType::kPassword, kLabel,
                    CreateMetadataWithType<PasswordMetadata>(),
                    {.state = TpmEccAuthBlockState()});

  auto delay_in_ms = driver.GetFactorDelay(kObfuscatedUser, factor);
  ASSERT_THAT(delay_in_ms, NotOk());
  EXPECT_THAT(delay_in_ms.status()->local_legacy_error(),
              Eq(user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
}

TEST_F(PinDriverTest, GetDelayFailsWithoutLeLabel) {
  PinAuthFactorDriver pin_driver(&crypto_);
  AuthFactorDriver& driver = pin_driver;

  AuthFactor factor(AuthFactorType::kPin, kLabel,
                    CreateMetadataWithType<PinMetadata>(),
                    {.state = PinWeaverAuthBlockState()});

  auto delay_in_ms = driver.GetFactorDelay(kObfuscatedUser, factor);
  ASSERT_THAT(delay_in_ms, NotOk());
  EXPECT_THAT(delay_in_ms.status()->local_legacy_error(),
              Eq(user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
}

TEST_F(PinDriverTest, GetDelayInfinite) {
  PinAuthFactorDriver pin_driver(&crypto_);
  AuthFactorDriver& driver = pin_driver;

  AuthFactor factor(AuthFactorType::kPin, kLabel,
                    CreateMetadataWithType<PinMetadata>(),
                    {.state = PinWeaverAuthBlockState({.le_label = kLeLabel})});
  EXPECT_CALL(hwsec_pw_manager_, GetDelayInSeconds(kLeLabel))
      .WillOnce(ReturnValue(std::numeric_limits<uint32_t>::max()));

  auto delay_in_ms = driver.GetFactorDelay(kObfuscatedUser, factor);
  ASSERT_THAT(delay_in_ms, IsOk());
  EXPECT_THAT(delay_in_ms->is_max(), IsTrue());
}

TEST_F(PinDriverTest, GetDelayFinite) {
  PinAuthFactorDriver pin_driver(&crypto_);
  AuthFactorDriver& driver = pin_driver;

  AuthFactor factor(AuthFactorType::kPin, kLabel,
                    CreateMetadataWithType<PinMetadata>(),
                    {.state = PinWeaverAuthBlockState({.le_label = kLeLabel})});
  EXPECT_CALL(hwsec_pw_manager_, GetDelayInSeconds(kLeLabel))
      .WillOnce(ReturnValue(10));

  auto delay_in_ms = driver.GetFactorDelay(kObfuscatedUser, factor);
  ASSERT_THAT(delay_in_ms, IsOk());
  EXPECT_THAT(*delay_in_ms, Eq(base::Seconds(10)));
}

TEST_F(PinDriverTest, GetDelayZero) {
  PinAuthFactorDriver pin_driver(&crypto_);
  AuthFactorDriver& driver = pin_driver;

  AuthFactor factor(AuthFactorType::kPin, kLabel,
                    CreateMetadataWithType<PinMetadata>(),
                    {.state = PinWeaverAuthBlockState({.le_label = kLeLabel})});
  EXPECT_CALL(hwsec_pw_manager_, GetDelayInSeconds(kLeLabel))
      .WillOnce(ReturnValue(0));

  auto delay_in_ms = driver.GetFactorDelay(kObfuscatedUser, factor);
  ASSERT_THAT(delay_in_ms, IsOk());
  EXPECT_THAT(delay_in_ms->is_zero(), IsTrue());
}

TEST_F(PinDriverTest, GetExpirationFails) {
  PinAuthFactorDriver pin_driver(&crypto_);
  AuthFactorDriver& driver = pin_driver;

  AuthFactor factor(AuthFactorType::kPin, kLabel,
                    CreateMetadataWithType<PinMetadata>(),
                    {.state = PinWeaverAuthBlockState()});

  auto delay = driver.GetTimeUntilExpiration(kObfuscatedUser, factor);
  ASSERT_THAT(delay, NotOk());
  EXPECT_THAT(delay.status()->local_legacy_error(),
              Eq(user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
}

TEST_F(PinDriverTest, CreateCredentialVerifierFails) {
  PinAuthFactorDriver pin_driver(&crypto_);
  AuthFactorDriver& driver = pin_driver;

  auto verifier = driver.CreateCredentialVerifier(kLabel, {}, {});
  EXPECT_THAT(verifier, IsNull());
}

}  // namespace
}  // namespace cryptohome
