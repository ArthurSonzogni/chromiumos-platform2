// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_factor/types/fingerprint.h"

#include <memory>
#include <utility>

#include <base/test/test_future.h>
#include <base/functional/callback.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libhwsec-foundation/error/testing_helper.h>

#include "cryptohome/auth_blocks/biometrics_auth_block_service.h"
#include "cryptohome/auth_blocks/mock_biometrics_command_processor.h"
#include "cryptohome/auth_factor/auth_factor_metadata.h"
#include "cryptohome/auth_factor/auth_factor_storage_type.h"
#include "cryptohome/auth_factor/auth_factor_type.h"
#include "cryptohome/auth_factor/types/interface.h"
#include "cryptohome/auth_factor/types/test_utils.h"
#include "cryptohome/flatbuffer_schemas/auth_block_state.h"
#include "cryptohome/mock_le_credential_manager.h"
#include "cryptohome/util/async_init.h"

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
using ::testing::Return;

class FingerprintDriverTest : public AuthFactorDriverGenericTest {
 protected:
  FingerprintDriverTest() {
    auto le_manager = std::make_unique<MockLECredentialManager>();
    le_manager_ = le_manager.get();
    crypto_.set_le_manager_for_testing(std::move(le_manager));

    auto processor = std::make_unique<MockBiometricsCommandProcessor>();
    bio_command_processor_ = processor.get();
    EXPECT_CALL(*bio_command_processor_, SetEnrollScanDoneCallback(_));
    EXPECT_CALL(*bio_command_processor_, SetAuthScanDoneCallback(_));
    EXPECT_CALL(*bio_command_processor_, SetSessionFailedCallback);
    bio_service_ = std::make_unique<BiometricsAuthBlockService>(
        std::move(processor), /*enroll_signal_sender=*/base::DoNothing(),
        /*auth_signal_sender=*/base::DoNothing());
  }

  MockLECredentialManager* le_manager_;
  MockBiometricsCommandProcessor* bio_command_processor_;
  std::unique_ptr<BiometricsAuthBlockService> bio_service_;
};

TEST_F(FingerprintDriverTest, ConvertToProto) {
  // Setup
  FingerprintAuthFactorDriver fp_driver(
      &crypto_, AsyncInitPtr<BiometricsAuthBlockService>(bio_service_.get()));
  AuthFactorDriver& driver = fp_driver;
  AuthFactorMetadata metadata =
      CreateMetadataWithType<FingerprintAuthFactorMetadata>();

  // Test
  std::optional<user_data_auth::AuthFactor> proto =
      driver.ConvertToProto(kLabel, metadata);

  // Verify
  ASSERT_THAT(proto, Optional(_));
  EXPECT_THAT(proto.value().type(),
              Eq(user_data_auth::AUTH_FACTOR_TYPE_FINGERPRINT));
  EXPECT_THAT(proto.value().label(), Eq(kLabel));
  EXPECT_THAT(proto->common_metadata().chromeos_version_last_updated(),
              Eq(kChromeosVersion));
  EXPECT_THAT(proto->common_metadata().chrome_version_last_updated(),
              Eq(kChromeVersion));
  EXPECT_THAT(proto->common_metadata().lockout_policy(),
              Eq(user_data_auth::LOCKOUT_POLICY_NONE));
  EXPECT_THAT(proto.value().has_fingerprint_metadata(), IsTrue());
}

TEST_F(FingerprintDriverTest, ConvertToProtoNullOpt) {
  // Setup
  FingerprintAuthFactorDriver fp_driver(
      &crypto_, AsyncInitPtr<BiometricsAuthBlockService>(bio_service_.get()));
  AuthFactorDriver& driver = fp_driver;
  AuthFactorMetadata metadata;

  // Test
  std::optional<user_data_auth::AuthFactor> proto =
      driver.ConvertToProto(kLabel, metadata);

  // Verify
  EXPECT_THAT(proto, Eq(std::nullopt));
}

TEST_F(FingerprintDriverTest, UnsupportedWithVk) {
  // Setup
  FingerprintAuthFactorDriver fp_driver(
      &crypto_, AsyncInitPtr<BiometricsAuthBlockService>(bio_service_.get()));
  AuthFactorDriver& driver = fp_driver;

  // Test, Verify.
  EXPECT_THAT(driver.IsSupported(AuthFactorStorageType::kVaultKeyset, {}),
              IsFalse());
}

TEST_F(FingerprintDriverTest, UnsupportedWithKiosk) {
  // Setup
  FingerprintAuthFactorDriver fp_driver(
      &crypto_, AsyncInitPtr<BiometricsAuthBlockService>(nullptr));
  AuthFactorDriver& driver = fp_driver;

  // Test, Verify.
  EXPECT_THAT(driver.IsSupported(AuthFactorStorageType::kUserSecretStash,
                                 {AuthFactorType::kKiosk}),
              IsFalse());
}

TEST_F(FingerprintDriverTest, UnsupportedByBlock) {
  // Setup
  FingerprintAuthFactorDriver fp_driver(
      &crypto_, AsyncInitPtr<BiometricsAuthBlockService>(nullptr));
  AuthFactorDriver& driver = fp_driver;

  // Test, Verify
  EXPECT_THAT(driver.IsSupported(AuthFactorStorageType::kUserSecretStash, {}),
              IsFalse());
}

TEST_F(FingerprintDriverTest, SupportedByBlock) {
  // Setup
  EXPECT_CALL(*bio_command_processor_, IsReady()).WillOnce(Return(true));
  EXPECT_CALL(hwsec_, IsReady()).WillOnce(ReturnValue(true));
  EXPECT_CALL(hwsec_, IsBiometricsPinWeaverEnabled())
      .WillOnce(ReturnValue(true));
  FingerprintAuthFactorDriver fp_driver(
      &crypto_, AsyncInitPtr<BiometricsAuthBlockService>(bio_service_.get()));
  AuthFactorDriver& driver = fp_driver;

  // Test, Verify
  EXPECT_THAT(driver.IsSupported(AuthFactorStorageType::kUserSecretStash, {}),
              IsTrue());
}

TEST_F(FingerprintDriverTest, PrepareForAddFailure) {
  // Setup.
  FingerprintAuthFactorDriver fp_driver(
      &crypto_, AsyncInitPtr<BiometricsAuthBlockService>(bio_service_.get()));
  AuthFactorDriver& driver = fp_driver;
  EXPECT_CALL(*bio_command_processor_, StartEnrollSession(_))
      .WillOnce([](auto&& callback) { std::move(callback).Run(false); });

  // Test.
  TestFuture<CryptohomeStatusOr<std::unique_ptr<PreparedAuthFactorToken>>>
      prepare_result;
  driver.PrepareForAdd(kObfuscatedUser, prepare_result.GetCallback());

  // Verify.
  EXPECT_THAT(prepare_result.Get(), NotOk());
  EXPECT_THAT(prepare_result.Get().status()->local_legacy_error(),
              Eq(user_data_auth::CRYPTOHOME_ERROR_FINGERPRINT_ERROR_INTERNAL));
}

TEST_F(FingerprintDriverTest, PrepareForAddSuccess) {
  // Setup.
  FingerprintAuthFactorDriver fp_driver(
      &crypto_, AsyncInitPtr<BiometricsAuthBlockService>(bio_service_.get()));
  AuthFactorDriver& driver = fp_driver;
  EXPECT_CALL(*bio_command_processor_, StartEnrollSession(_))
      .WillOnce([](auto&& callback) { std::move(callback).Run(true); });

  // Test.
  TestFuture<CryptohomeStatusOr<std::unique_ptr<PreparedAuthFactorToken>>>
      prepare_result;
  driver.PrepareForAdd(kObfuscatedUser, prepare_result.GetCallback());

  // Verify.
  EXPECT_THAT(prepare_result.Get(), IsOk());
}

TEST_F(FingerprintDriverTest, PrepareForAuthFailure) {
  // Setup.
  FingerprintAuthFactorDriver fp_driver(
      &crypto_, AsyncInitPtr<BiometricsAuthBlockService>(bio_service_.get()));
  AuthFactorDriver& driver = fp_driver;
  EXPECT_CALL(*bio_command_processor_,
              StartAuthenticateSession(kObfuscatedUser, _))
      .WillOnce(
          [](auto&&, auto&& callback) { std::move(callback).Run(false); });

  // Test.
  TestFuture<CryptohomeStatusOr<std::unique_ptr<PreparedAuthFactorToken>>>
      prepare_result;
  driver.PrepareForAuthenticate(kObfuscatedUser, prepare_result.GetCallback());

  // Verify.
  EXPECT_THAT(prepare_result.Get(), NotOk());
  EXPECT_THAT(prepare_result.Get().status()->local_legacy_error(),
              Eq(user_data_auth::CRYPTOHOME_ERROR_FINGERPRINT_ERROR_INTERNAL));
}

TEST_F(FingerprintDriverTest, PrepareForAuthSuccess) {
  // Setup.
  FingerprintAuthFactorDriver fp_driver(
      &crypto_, AsyncInitPtr<BiometricsAuthBlockService>(bio_service_.get()));
  AuthFactorDriver& driver = fp_driver;
  EXPECT_CALL(*bio_command_processor_,
              StartAuthenticateSession(kObfuscatedUser, _))
      .WillOnce([](auto&&, auto&& callback) { std::move(callback).Run(true); });

  // Test.
  TestFuture<CryptohomeStatusOr<std::unique_ptr<PreparedAuthFactorToken>>>
      prepare_result;
  driver.PrepareForAuthenticate(kObfuscatedUser, prepare_result.GetCallback());

  // Verify.
  EXPECT_THAT(prepare_result.Get(), IsOk());
}

TEST_F(FingerprintDriverTest, GetDelayFails) {
  FingerprintAuthFactorDriver fp_driver(
      &crypto_, AsyncInitPtr<BiometricsAuthBlockService>(bio_service_.get()));
  AuthFactorDriver& driver = fp_driver;

  AuthFactor factor(AuthFactorType::kFingerprint, kLabel,
                    CreateMetadataWithType<FingerprintAuthFactorMetadata>(),
                    {.state = FingerprintAuthBlockState()});

  auto delay_in_ms = driver.GetFactorDelay(factor);
  ASSERT_THAT(delay_in_ms, NotOk());
  EXPECT_THAT(delay_in_ms.status()->local_legacy_error(),
              Eq(user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
}

TEST_F(FingerprintDriverTest, CreateCredentialVerifierFails) {
  FingerprintAuthFactorDriver fp_driver(
      &crypto_, AsyncInitPtr<BiometricsAuthBlockService>(bio_service_.get()));
  AuthFactorDriver& driver = fp_driver;

  auto verifier = driver.CreateCredentialVerifier(kLabel, {});
  EXPECT_THAT(verifier, IsNull());
}

}  // namespace
}  // namespace cryptohome
