// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_factor/types/fingerprint.h"

#include <limits>
#include <memory>
#include <utility>

#include <base/files/file_path.h>
#include <base/functional/callback.h>
#include <base/test/test_future.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libhwsec-foundation/crypto/aes.h>
#include <libhwsec-foundation/error/testing_helper.h>

#include "cryptohome/auth_blocks/biometrics_auth_block_service.h"
#include "cryptohome/auth_blocks/mock_biometrics_command_processor.h"
#include "cryptohome/auth_factor/metadata.h"
#include "cryptohome/auth_factor/storage_type.h"
#include "cryptohome/auth_factor/type.h"
#include "cryptohome/auth_factor/types/interface.h"
#include "cryptohome/auth_factor/types/test_utils.h"
#include "cryptohome/flatbuffer_schemas/auth_block_state.h"
#include "cryptohome/flatbuffer_schemas/auth_factor.h"
#include "cryptohome/mock_platform.h"
#include "cryptohome/user_secret_stash/storage.h"
#include "cryptohome/util/async_init.h"

namespace cryptohome {
namespace {

using ::base::test::TestFuture;
using ::cryptohome::error::CryptohomeError;
using ::cryptohome::error::ErrorActionSet;
using ::hwsec::PinWeaverManagerFrontend::AuthChannel::kFingerprintAuthChannel;
using ::hwsec_foundation::kAesGcmIVSize;
using ::hwsec_foundation::kAesGcmTagSize;
using ::hwsec_foundation::error::testing::IsOk;
using ::hwsec_foundation::error::testing::NotOk;
using ::hwsec_foundation::error::testing::ReturnError;
using ::hwsec_foundation::error::testing::ReturnValue;
using ::testing::_;
using ::testing::Eq;
using ::testing::IsFalse;
using ::testing::IsNull;
using ::testing::IsTrue;
using ::testing::NiceMock;
using ::testing::Optional;
using ::testing::Return;

class FingerprintDriverTest : public AuthFactorDriverGenericTest {
 protected:
  const error::CryptohomeError::ErrorLocationPair kErrorLocationPlaceholder =
      error::CryptohomeError::ErrorLocationPair(
          static_cast<::cryptohome::error::CryptohomeError::ErrorLocation>(1),
          "Testing1");
  static constexpr uint64_t kLeLabel = 0xdeadbeefbaadf00d;

  FingerprintDriverTest() {
    auto processor = std::make_unique<MockBiometricsCommandProcessor>();
    bio_command_processor_ = processor.get();
    EXPECT_CALL(*bio_command_processor_, SetEnrollScanDoneCallback(_));
    EXPECT_CALL(*bio_command_processor_, SetAuthScanDoneCallback(_));
    EXPECT_CALL(*bio_command_processor_, SetSessionFailedCallback(_));
    bio_service_ = std::make_unique<BiometricsAuthBlockService>(
        std::move(processor), /*enroll_signal_sender=*/base::DoNothing(),
        /*auth_signal_sender=*/base::DoNothing());
  }

  // Create a USS that contains kleLabel as the rate limiter ID.
  void CreateUssWithRateLimiterId() {
    EncryptedUss::Container uss_container = {
        .ciphertext = brillo::BlobFromString("encrypted bytes!"),
        .iv = brillo::BlobFromString(std::string(kAesGcmIVSize, '\x0a')),
        .gcm_tag = brillo::BlobFromString(std::string(kAesGcmTagSize, '\x0b')),
        .created_on_os_version = "1.2.3.4",
        .user_metadata =
            {
                .fingerprint_rate_limiter_id = kLeLabel,
            },
    };
    EncryptedUss uss(uss_container);
    UserUssStorage user_uss_storage(uss_storage_, kObfuscatedUser);
    ASSERT_THAT(uss.ToStorage(user_uss_storage), IsOk());
  }

  NiceMock<MockPlatform> platform_;
  UssStorage uss_storage_{&platform_};
  MockBiometricsCommandProcessor* bio_command_processor_;
  std::unique_ptr<BiometricsAuthBlockService> bio_service_;
};

TEST_F(FingerprintDriverTest, ConvertToProto) {
  // Setup
  FingerprintAuthFactorDriver fp_driver(
      &platform_, &crypto_, &uss_storage_,
      AsyncInitPtr<BiometricsAuthBlockService>(bio_service_.get()));
  AuthFactorDriver& driver = fp_driver;
  AuthFactorMetadata metadata = CreateMetadataWithType<FingerprintMetadata>();

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
      &platform_, &crypto_, &uss_storage_,
      AsyncInitPtr<BiometricsAuthBlockService>(bio_service_.get()));
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
      &platform_, &crypto_, &uss_storage_,
      AsyncInitPtr<BiometricsAuthBlockService>(bio_service_.get()));
  AuthFactorDriver& driver = fp_driver;

  // Test, Verify.
  EXPECT_THAT(
      driver.IsSupportedByStorage({AuthFactorStorageType::kVaultKeyset}, {}),
      IsFalse());
}

TEST_F(FingerprintDriverTest, SupportedWithVkUssMix) {
  // Setup
  FingerprintAuthFactorDriver fp_driver(
      &platform_, &crypto_, &uss_storage_,
      AsyncInitPtr<BiometricsAuthBlockService>(bio_service_.get()));
  AuthFactorDriver& driver = fp_driver;

  // Test, Verify.
  EXPECT_THAT(
      driver.IsSupportedByStorage({AuthFactorStorageType::kVaultKeyset,
                                   AuthFactorStorageType::kUserSecretStash},
                                  {}),
      IsTrue());
}

TEST_F(FingerprintDriverTest, UnsupportedWithKiosk) {
  // Setup
  FingerprintAuthFactorDriver fp_driver(
      &platform_, &crypto_, &uss_storage_,
      AsyncInitPtr<BiometricsAuthBlockService>(bio_service_.get()));
  AuthFactorDriver& driver = fp_driver;

  // Test, Verify.
  EXPECT_THAT(
      driver.IsSupportedByStorage({AuthFactorStorageType::kUserSecretStash},
                                  {AuthFactorType::kKiosk}),
      IsFalse());
}

TEST_F(FingerprintDriverTest, UnsupportedByBlock) {
  // Setup
  FingerprintAuthFactorDriver fp_driver(
      &platform_, &crypto_, &uss_storage_,
      AsyncInitPtr<BiometricsAuthBlockService>(bio_service_.get()));
  AuthFactorDriver& driver = fp_driver;

  // Test, Verify
  EXPECT_THAT(driver.IsSupportedByHardware(), IsFalse());
}

TEST_F(FingerprintDriverTest, SupportedByBlock) {
  // Setup
  EXPECT_CALL(*bio_command_processor_, IsReady()).WillOnce(Return(true));
  EXPECT_CALL(hwsec_, IsReady()).WillOnce(ReturnValue(true));
  EXPECT_CALL(hwsec_, IsBiometricsPinWeaverEnabled())
      .WillOnce(ReturnValue(true));
  FingerprintAuthFactorDriver fp_driver(
      &platform_, &crypto_, &uss_storage_,
      AsyncInitPtr<BiometricsAuthBlockService>(bio_service_.get()));
  AuthFactorDriver& driver = fp_driver;

  // Test, Verify
  EXPECT_THAT(driver.IsSupportedByHardware(), IsTrue());
}

TEST_F(FingerprintDriverTest, PrepareForAddFailure) {
  const brillo::SecureBlob kResetSecret(32, 1);
  const brillo::Blob kNonce(32, 2);
  // Setup.
  FingerprintAuthFactorDriver fp_driver(
      &platform_, &crypto_, &uss_storage_,
      AsyncInitPtr<BiometricsAuthBlockService>(bio_service_.get()));
  AuthFactorDriver& driver = fp_driver;
  EXPECT_CALL(*bio_command_processor_, GetNonce(_))
      .WillOnce(
          [&kNonce](auto&& callback) { std::move(callback).Run(kNonce); });
  EXPECT_CALL(hwsec_pw_manager_,
              StartBiometricsAuth(kFingerprintAuthChannel, kLeLabel, kNonce))
      .WillOnce(
          Return(hwsec::PinWeaverManagerFrontend::StartBiometricsAuthReply{}));
  EXPECT_CALL(*bio_command_processor_, StartEnrollSession(_, _))
      .WillOnce(
          [](auto&&, auto&& callback) { std::move(callback).Run(false); });

  // Test.
  TestFuture<CryptohomeStatusOr<std::unique_ptr<PreparedAuthFactorToken>>>
      prepare_result;
  AuthInput auth_input{
      .obfuscated_username = kObfuscatedUser,
      .reset_secret = kResetSecret,
      .rate_limiter_label = kLeLabel,
  };
  driver.PrepareForAdd(auth_input, prepare_result.GetCallback());

  // Verify.
  EXPECT_THAT(prepare_result.Get(), NotOk());
  EXPECT_THAT(prepare_result.Get().status()->local_legacy_error(),
              Eq(user_data_auth::CRYPTOHOME_ERROR_FINGERPRINT_ERROR_INTERNAL));
}

TEST_F(FingerprintDriverTest, PrepareForAddSuccess) {
  const brillo::SecureBlob kResetSecret(32, 1);
  const brillo::Blob kNonce(32, 2);
  // Setup.
  FingerprintAuthFactorDriver fp_driver(
      &platform_, &crypto_, &uss_storage_,
      AsyncInitPtr<BiometricsAuthBlockService>(bio_service_.get()));
  AuthFactorDriver& driver = fp_driver;
  EXPECT_CALL(*bio_command_processor_, GetNonce(_))
      .WillOnce(
          [&kNonce](auto&& callback) { std::move(callback).Run(kNonce); });
  EXPECT_CALL(hwsec_pw_manager_,
              StartBiometricsAuth(kFingerprintAuthChannel, kLeLabel, kNonce))
      .WillOnce(
          Return(hwsec::PinWeaverManagerFrontend::StartBiometricsAuthReply{}));
  EXPECT_CALL(*bio_command_processor_, StartEnrollSession(_, _))
      .WillOnce([](auto&&, auto&& callback) { std::move(callback).Run(true); });

  // Test.
  TestFuture<CryptohomeStatusOr<std::unique_ptr<PreparedAuthFactorToken>>>
      prepare_result;
  AuthInput auth_input{
      .obfuscated_username = kObfuscatedUser,
      .reset_secret = kResetSecret,
      .rate_limiter_label = kLeLabel,
  };
  driver.PrepareForAdd(auth_input, prepare_result.GetCallback());

  // Verify.
  EXPECT_THAT(prepare_result.Get(), IsOk());
}

TEST_F(FingerprintDriverTest, PrepareForAuthenticateFailure) {
  const brillo::Blob kNonce(32, 2);
  // Setup.
  FingerprintAuthFactorDriver fp_driver(
      &platform_, &crypto_, &uss_storage_,
      AsyncInitPtr<BiometricsAuthBlockService>(bio_service_.get()));
  AuthFactorDriver& driver = fp_driver;
  EXPECT_CALL(*bio_command_processor_, GetNonce(_))
      .WillOnce(
          [&kNonce](auto&& callback) { std::move(callback).Run(kNonce); });
  EXPECT_CALL(hwsec_pw_manager_,
              StartBiometricsAuth(kFingerprintAuthChannel, kLeLabel, kNonce))
      .WillOnce(
          Return(hwsec::PinWeaverManagerFrontend::StartBiometricsAuthReply{}));
  EXPECT_CALL(*bio_command_processor_, StartAuthenticateSession(_, _, _))
      .WillOnce([](auto&&, auto&&, auto&& callback) {
        std::move(callback).Run(false);
      });

  // Test.
  TestFuture<CryptohomeStatusOr<std::unique_ptr<PreparedAuthFactorToken>>>
      prepare_result;
  AuthInput auth_input{
      .obfuscated_username = kObfuscatedUser,
      .rate_limiter_label = kLeLabel,
  };
  driver.PrepareForAuthenticate(auth_input, prepare_result.GetCallback());

  // Verify.
  EXPECT_THAT(prepare_result.Get(), NotOk());
  EXPECT_THAT(prepare_result.Get().status()->local_legacy_error(),
              Eq(user_data_auth::CRYPTOHOME_ERROR_FINGERPRINT_ERROR_INTERNAL));
}

TEST_F(FingerprintDriverTest, PrepareForAuthenticateSuccess) {
  const brillo::Blob kNonce(32, 2);
  // Setup.
  FingerprintAuthFactorDriver fp_driver(
      &platform_, &crypto_, &uss_storage_,
      AsyncInitPtr<BiometricsAuthBlockService>(bio_service_.get()));
  AuthFactorDriver& driver = fp_driver;
  EXPECT_CALL(*bio_command_processor_, GetNonce(_))
      .WillOnce(
          [&kNonce](auto&& callback) { std::move(callback).Run(kNonce); });
  EXPECT_CALL(hwsec_pw_manager_,
              StartBiometricsAuth(kFingerprintAuthChannel, kLeLabel, kNonce))
      .WillOnce(
          Return(hwsec::PinWeaverManagerFrontend::StartBiometricsAuthReply{}));
  EXPECT_CALL(*bio_command_processor_, StartAuthenticateSession(_, _, _))
      .WillOnce([](auto&&, auto&&, auto&& callback) {
        std::move(callback).Run(true);
      });

  // Test.
  TestFuture<CryptohomeStatusOr<std::unique_ptr<PreparedAuthFactorToken>>>
      prepare_result;
  AuthInput auth_input{
      .obfuscated_username = kObfuscatedUser,
      .rate_limiter_label = kLeLabel,
  };
  driver.PrepareForAuthenticate(auth_input, prepare_result.GetCallback());

  // Verify.
  EXPECT_THAT(prepare_result.Get(), IsOk());
}

TEST_F(FingerprintDriverTest, GetDelayFailsWithoutLeLabel) {
  FingerprintAuthFactorDriver fp_driver(
      &platform_, &crypto_, &uss_storage_,
      AsyncInitPtr<BiometricsAuthBlockService>(bio_service_.get()));
  AuthFactorDriver& driver = fp_driver;

  AuthFactor factor(AuthFactorType::kFingerprint, kLabel,
                    CreateMetadataWithType<FingerprintMetadata>(),
                    {.state = FingerprintAuthBlockState()});

  auto delay_in_ms = driver.GetFactorDelay(kObfuscatedUser, factor);
  ASSERT_THAT(delay_in_ms, NotOk());
  EXPECT_THAT(delay_in_ms.status()->local_legacy_error(),
              Eq(user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE));
}

TEST_F(FingerprintDriverTest, GetDelayInfinite) {
  FingerprintAuthFactorDriver fp_driver(
      &platform_, &crypto_, &uss_storage_,
      AsyncInitPtr<BiometricsAuthBlockService>(bio_service_.get()));
  AuthFactorDriver& driver = fp_driver;

  AuthFactor factor(AuthFactorType::kFingerprint, kLabel,
                    CreateMetadataWithType<FingerprintMetadata>(),
                    {.state = FingerprintAuthBlockState()});
  CreateUssWithRateLimiterId();
  EXPECT_CALL(hwsec_pw_manager_, GetDelayInSeconds(kLeLabel))
      .WillOnce(ReturnValue(std::numeric_limits<uint32_t>::max()));

  auto delay_in_ms = driver.GetFactorDelay(kObfuscatedUser, factor);
  ASSERT_THAT(delay_in_ms, IsOk());
  EXPECT_THAT(delay_in_ms->is_max(), IsTrue());
}

TEST_F(FingerprintDriverTest, GetDelayFinite) {
  FingerprintAuthFactorDriver fp_driver(
      &platform_, &crypto_, &uss_storage_,
      AsyncInitPtr<BiometricsAuthBlockService>(bio_service_.get()));
  AuthFactorDriver& driver = fp_driver;

  AuthFactor factor(AuthFactorType::kFingerprint, kLabel,
                    CreateMetadataWithType<FingerprintMetadata>(),
                    {.state = FingerprintAuthBlockState()});
  CreateUssWithRateLimiterId();
  EXPECT_CALL(hwsec_pw_manager_, GetDelayInSeconds(kLeLabel))
      .WillOnce(ReturnValue(10));

  auto delay_in_ms = driver.GetFactorDelay(kObfuscatedUser, factor);
  ASSERT_THAT(delay_in_ms, IsOk());
  EXPECT_THAT(*delay_in_ms, Eq(base::Seconds(10)));
}

TEST_F(FingerprintDriverTest, GetDelayZero) {
  FingerprintAuthFactorDriver fp_driver(
      &platform_, &crypto_, &uss_storage_,
      AsyncInitPtr<BiometricsAuthBlockService>(bio_service_.get()));
  AuthFactorDriver& driver = fp_driver;

  AuthFactor factor(AuthFactorType::kFingerprint, kLabel,
                    CreateMetadataWithType<FingerprintMetadata>(),
                    {.state = FingerprintAuthBlockState()});
  CreateUssWithRateLimiterId();
  EXPECT_CALL(hwsec_pw_manager_, GetDelayInSeconds(kLeLabel))
      .WillOnce(ReturnValue(0));

  auto delay_in_ms = driver.GetFactorDelay(kObfuscatedUser, factor);
  ASSERT_THAT(delay_in_ms, IsOk());
  EXPECT_THAT(delay_in_ms->is_zero(), IsTrue());
}

TEST_F(FingerprintDriverTest, IsExpiredFailsWithoutLeLabel) {
  FingerprintAuthFactorDriver fp_driver(
      &platform_, &crypto_, &uss_storage_,
      AsyncInitPtr<BiometricsAuthBlockService>(bio_service_.get()));
  AuthFactorDriver& driver = fp_driver;

  AuthFactor factor(AuthFactorType::kFingerprint, kLabel,
                    CreateMetadataWithType<FingerprintMetadata>(),
                    {.state = FingerprintAuthBlockState()});

  auto is_expired = driver.IsExpired(kObfuscatedUser, factor);
  ASSERT_THAT(is_expired, NotOk());
  EXPECT_THAT(is_expired.status()->local_legacy_error(),
              Eq(user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE));
}

TEST_F(FingerprintDriverTest, IsNotExpired) {
  FingerprintAuthFactorDriver fp_driver(
      &platform_, &crypto_, &uss_storage_,
      AsyncInitPtr<BiometricsAuthBlockService>(bio_service_.get()));
  AuthFactorDriver& driver = fp_driver;

  AuthFactor factor(AuthFactorType::kFingerprint, kLabel,
                    CreateMetadataWithType<FingerprintMetadata>(),
                    {.state = FingerprintAuthBlockState()});
  CreateUssWithRateLimiterId();
  EXPECT_CALL(hwsec_pw_manager_, GetExpirationInSeconds(kLeLabel))
      .WillOnce(ReturnValue(10));

  auto is_expired = driver.IsExpired(kObfuscatedUser, factor);
  ASSERT_THAT(is_expired, IsOk());
  EXPECT_FALSE(*is_expired);
}

TEST_F(FingerprintDriverTest, IsExpired) {
  FingerprintAuthFactorDriver fp_driver(
      &platform_, &crypto_, &uss_storage_,
      AsyncInitPtr<BiometricsAuthBlockService>(bio_service_.get()));
  AuthFactorDriver& driver = fp_driver;

  AuthFactor factor(AuthFactorType::kFingerprint, kLabel,
                    CreateMetadataWithType<FingerprintMetadata>(),
                    {.state = FingerprintAuthBlockState()});
  CreateUssWithRateLimiterId();
  EXPECT_CALL(hwsec_pw_manager_, GetExpirationInSeconds(kLeLabel))
      .WillOnce(ReturnValue(0));

  auto is_expired = driver.IsExpired(kObfuscatedUser, factor);
  ASSERT_THAT(is_expired, IsOk());
  EXPECT_TRUE(*is_expired);
}

TEST_F(FingerprintDriverTest, CreateCredentialVerifierFails) {
  FingerprintAuthFactorDriver fp_driver(
      &platform_, &crypto_, &uss_storage_,
      AsyncInitPtr<BiometricsAuthBlockService>(bio_service_.get()));
  AuthFactorDriver& driver = fp_driver;

  auto verifier = driver.CreateCredentialVerifier(kLabel, {});
  EXPECT_THAT(verifier, IsNull());
}

}  // namespace
}  // namespace cryptohome
