// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_factor/types/cryptohome_recovery.h"

#include <memory>
#include <string>
#include <utility>

#include <base/test/test_future.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libhwsec-foundation/error/testing_helper.h>
#include <libhwsec-foundation/status/status_chain.h>
#include <libstorage/platform/mock_platform.h>

#include "cryptohome/auth_blocks/mock_cryptohome_recovery_service.h"
#include "cryptohome/auth_blocks/prepare_token.h"
#include "cryptohome/auth_factor/storage_type.h"
#include "cryptohome/auth_factor/type.h"
#include "cryptohome/auth_factor/types/interface.h"
#include "cryptohome/auth_factor/types/test_utils.h"
#include "cryptohome/error/cryptohome_error.h"
#include "cryptohome/filesystem_layout.h"
#include "cryptohome/flatbuffer_schemas/auth_block_state.h"
#include "cryptohome/flatbuffer_schemas/auth_factor.h"

namespace cryptohome {
namespace {

constexpr char kPublicKeyStr[] = "1a2b3c4d5e6f";

using ::base::test::TestFuture;
using ::cryptohome::error::CryptohomeError;
using ::hwsec_foundation::error::testing::IsOk;
using ::hwsec_foundation::error::testing::NotOk;
using ::hwsec_foundation::error::testing::ReturnValue;
using ::hwsec_foundation::status::OkStatus;
using ::testing::_;
using ::testing::Eq;
using ::testing::IsFalse;
using ::testing::IsNull;
using ::testing::IsTrue;
using ::testing::NiceMock;
using ::testing::NotNull;
using ::testing::Optional;

// Minimal prepare token class. Does nothing for termination.
class TestToken : public PreparedAuthFactorToken {
 public:
  using PreparedAuthFactorToken::PreparedAuthFactorToken;

  // These operations are trivial. Initializing |this| token is good enough.
  bool IsTokenFullyPrepared() override { return true; }
  bool IsReadyForClient() override { return true; }

 private:
  CryptohomeStatus TerminateAuthFactor() override {
    return OkStatus<CryptohomeError>();
  }
};

class CryptohomeRecoveryDriverTest : public AuthFactorDriverGenericTest {
 protected:
  NiceMock<libstorage::MockPlatform> platform_;
  NiceMock<MockCryptohomeRecoveryAuthBlockService> service_{
      &platform_, &recovery_frontend_};
};

TEST_F(CryptohomeRecoveryDriverTest, ConvertToProto) {
  // Setup
  CryptohomeRecoveryAuthFactorDriver recovery_driver(&platform_, &crypto_,
                                                     &service_);
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
  CryptohomeRecoveryAuthFactorDriver recovery_driver(&platform_, &crypto_,
                                                     &service_);
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
  CryptohomeRecoveryAuthFactorDriver recovery_driver(&platform_, &crypto_,
                                                     &service_);
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
  CryptohomeRecoveryAuthFactorDriver recovery_driver(&platform_, &crypto_,
                                                     &service_);
  AuthFactorDriver& driver = recovery_driver;

  // Test, Verify.
  EXPECT_THAT(
      driver.IsSupportedByStorage({AuthFactorStorageType::kVaultKeyset}, {}),
      IsFalse());
}

TEST_F(CryptohomeRecoveryDriverTest, UnsupportedWithKiosk) {
  // Setup
  CryptohomeRecoveryAuthFactorDriver recovery_driver(&platform_, &crypto_,
                                                     &service_);
  AuthFactorDriver& driver = recovery_driver;

  // Test, Verify.
  EXPECT_THAT(
      driver.IsSupportedByStorage({AuthFactorStorageType::kUserSecretStash},
                                  {AuthFactorType::kKiosk}),
      IsFalse());
}

TEST_F(CryptohomeRecoveryDriverTest, SupportedWithVkUssMix) {
  // Setup
  CryptohomeRecoveryAuthFactorDriver recovery_driver(&platform_, &crypto_,
                                                     &service_);
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
  CryptohomeRecoveryAuthFactorDriver recovery_driver(&platform_, &crypto_,
                                                     &service_);
  AuthFactorDriver& driver = recovery_driver;

  // Test, Verify
  EXPECT_THAT(driver.IsSupportedByHardware(), IsFalse());
}

TEST_F(CryptohomeRecoveryDriverTest, SupportedByBlock) {
  // Setup
  EXPECT_CALL(hwsec_, IsReady()).WillOnce(ReturnValue(true));
  CryptohomeRecoveryAuthFactorDriver recovery_driver(&platform_, &crypto_,
                                                     &service_);
  AuthFactorDriver& driver = recovery_driver;

  // Test, Verify
  EXPECT_THAT(driver.IsSupportedByHardware(), IsTrue());
}

TEST_F(CryptohomeRecoveryDriverTest, PrepareForAddFails) {
  CryptohomeRecoveryAuthFactorDriver recovery_driver(&platform_, &crypto_,
                                                     &service_);
  AuthFactorDriver& driver = recovery_driver;

  TestFuture<CryptohomeStatusOr<std::unique_ptr<PreparedAuthFactorToken>>>
      prepare_result;
  PrepareInput prepare_input{.username = kObfuscatedUser};
  driver.PrepareForAdd(prepare_input, prepare_result.GetCallback());
  EXPECT_THAT(prepare_result.Get().status()->local_legacy_error(),
              Eq(user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
}

TEST_F(CryptohomeRecoveryDriverTest, PrepareForAuthFailsWithNoInput) {
  CryptohomeRecoveryAuthFactorDriver recovery_driver(&platform_, &crypto_,
                                                     &service_);
  AuthFactorDriver& driver = recovery_driver;

  TestFuture<CryptohomeStatusOr<std::unique_ptr<PreparedAuthFactorToken>>>
      prepare_result;
  PrepareInput prepare_input{.username = kObfuscatedUser};
  driver.PrepareForAuthenticate(prepare_input, prepare_result.GetCallback());
  EXPECT_THAT(prepare_result.Get().status()->local_legacy_error(),
              Eq(user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
}

TEST_F(CryptohomeRecoveryDriverTest, PrepareForAuthSuccess) {
  CryptohomeRecoveryAuthFactorDriver recovery_driver(&platform_, &crypto_,
                                                     &service_);
  AuthFactorDriver& driver = recovery_driver;

  EXPECT_CALL(service_, GenerateRecoveryRequest(_, _, _, _, _))
      .WillOnce([](const ObfuscatedUsername&,
                   const cryptorecovery::RequestMetadata&, const brillo::Blob&,
                   const CryptohomeRecoveryAuthBlockState&,
                   PreparedAuthFactorToken::Consumer on_done) {
        PrepareOutput prepare_output = {
            .cryptohome_recovery_prepare_output =
                CryptohomeRecoveryPrepareOutput{},
        };
        std::move(on_done).Run(std::make_unique<TestToken>(
            AuthFactorType::kCryptohomeRecovery, std::move(prepare_output)));
      });

  CryptohomeRecoveryPrepareInput recovery_input;
  TestFuture<CryptohomeStatusOr<std::unique_ptr<PreparedAuthFactorToken>>>
      prepare_result;
  PrepareInput prepare_input{
      .username = kObfuscatedUser,
      .cryptohome_recovery_prepare_input = std::move(recovery_input)};
  driver.PrepareForAuthenticate(prepare_input, prepare_result.GetCallback());
  ASSERT_THAT(prepare_result.Get().status(), IsOk());
  EXPECT_THAT((**prepare_result.Get())
                  .prepare_output()
                  .cryptohome_recovery_prepare_output,
              Optional(_));
}

TEST_F(CryptohomeRecoveryDriverTest, GetDelayMaxWhenLocked) {
  CryptohomeRecoveryAuthFactorDriver recovery_driver(&platform_, &crypto_,
                                                     &service_);
  AuthFactorDriver& driver = recovery_driver;

  AuthFactor factor(AuthFactorType::kCryptohomeRecovery, kLabel,
                    CreateMetadataWithType<CryptohomeRecoveryMetadata>(),
                    {.state = CryptohomeRecoveryAuthBlockState()});

  EXPECT_CALL(platform_, FileExists(GetRecoveryFactorLockPath()))
      .WillOnce(Return(true));
  auto delay_in_ms = driver.GetFactorDelay(kObfuscatedUser, factor);
  ASSERT_THAT(delay_in_ms, IsOk());
  EXPECT_THAT(delay_in_ms->is_max(), IsTrue());
}

TEST_F(CryptohomeRecoveryDriverTest, GetDelayZeroWhenNotLocked) {
  CryptohomeRecoveryAuthFactorDriver recovery_driver(&platform_, &crypto_,
                                                     &service_);
  AuthFactorDriver& driver = recovery_driver;

  AuthFactor factor(AuthFactorType::kCryptohomeRecovery, kLabel,
                    CreateMetadataWithType<CryptohomeRecoveryMetadata>(),
                    {.state = CryptohomeRecoveryAuthBlockState()});

  EXPECT_CALL(platform_, FileExists(GetRecoveryFactorLockPath()))
      .WillOnce(Return(false));
  auto delay_in_ms = driver.GetFactorDelay(kObfuscatedUser, factor);
  ASSERT_THAT(delay_in_ms, IsOk());
  EXPECT_THAT(delay_in_ms->is_zero(), IsTrue());
}

TEST_F(CryptohomeRecoveryDriverTest, GetExpirationFails) {
  CryptohomeRecoveryAuthFactorDriver recovery_driver(&platform_, &crypto_,
                                                     &service_);
  AuthFactorDriver& driver = recovery_driver;

  AuthFactor factor(AuthFactorType::kCryptohomeRecovery, kLabel,
                    CreateMetadataWithType<CryptohomeRecoveryMetadata>(),
                    {.state = CryptohomeRecoveryAuthBlockState()});

  auto delay = driver.GetTimeUntilExpiration(kObfuscatedUser, factor);
  ASSERT_THAT(delay, NotOk());
  EXPECT_THAT(delay.status()->local_legacy_error(),
              Eq(user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
}

TEST_F(CryptohomeRecoveryDriverTest, CreateCredentialVerifierFails) {
  CryptohomeRecoveryAuthFactorDriver recovery_driver(&platform_, &crypto_,
                                                     &service_);
  AuthFactorDriver& driver = recovery_driver;

  auto verifier = driver.CreateCredentialVerifier(kLabel, {}, {});
  EXPECT_THAT(verifier, IsNull());
}

}  // namespace
}  // namespace cryptohome
