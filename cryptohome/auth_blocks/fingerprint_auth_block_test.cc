// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_blocks/fingerprint_auth_block.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <brillo/secure_blob.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libhwsec-foundation/error/testing_helper.h>

#include "cryptohome/auth_blocks/auth_block.h"
#include "cryptohome/auth_blocks/mock_biometrics_command_processor.h"
#include "cryptohome/error/cryptohome_crypto_error.h"
#include "cryptohome/error/cryptohome_le_cred_error.h"
#include "cryptohome/error/utilities.h"
#include "cryptohome/flatbuffer_schemas/auth_block_state.h"
#include "cryptohome/le_credential_manager.h"
#include "cryptohome/mock_le_credential_manager.h"

namespace cryptohome {
namespace {

using base::test::TestFuture;
using cryptohome::error::ContainsActionInStack;
using cryptohome::error::CryptohomeError;
using cryptohome::error::CryptohomeLECredError;
using cryptohome::error::ErrorAction;
using cryptohome::error::ErrorActionSet;
using hwsec_foundation::error::testing::IsOk;
using hwsec_foundation::error::testing::OkStatus;
using hwsec_foundation::error::testing::ReturnError;
using hwsec_foundation::error::testing::ReturnOk;
using hwsec_foundation::error::testing::ReturnValue;
using hwsec_foundation::status::MakeStatus;
using user_data_auth::AuthEnrollmentProgress;
using user_data_auth::AuthScanDone;

using testing::_;
using testing::AllOf;
using testing::AnyNumber;
using testing::DoAll;
using testing::Field;
using testing::SaveArg;
using testing::SetArgPointee;
using testing::SizeIs;
using testing::StrictMock;

using OperationInput = BiometricsAuthBlockService::OperationInput;
using OperationOutput = BiometricsAuthBlockService::OperationOutput;

using CreateTestFuture = TestFuture<CryptohomeStatus,
                                    std::unique_ptr<KeyBlobs>,
                                    std::unique_ptr<AuthBlockState>>;
using SelectFactorTestFuture = TestFuture<CryptohomeStatus,
                                          std::optional<AuthInput>,
                                          std::optional<AuthFactor>>;

using DeriveTestFuture =
    TestFuture<CryptohomeStatus, std::unique_ptr<KeyBlobs>>;

constexpr uint8_t kFingerprintAuthChannel = 0;

constexpr uint64_t kFakeRateLimiterLabel = 100;
constexpr uint64_t kFakeCredLabel = 200;

constexpr char kFakeRecordId[] = "fake_record_id";
constexpr char kFakeRecordId2[] = "fake_record_id_2";

constexpr char kFakeAuthFactorLabel1[] = "fake_label_1";
constexpr char kFakeAuthFactorLabel2[] = "fake_label_2";

// TODO(b/247704971): Blob should be used for fields that doesn't contain secret
// values. Before the LE manager interface changes accordingly, transform the
// blob types explicitly.
brillo::SecureBlob BlobToSecureBlob(const brillo::Blob& blob) {
  return brillo::SecureBlob(blob.begin(), blob.end());
}

AuthBlockState GetFingerprintStateWithRecordId(std::string record_id) {
  AuthBlockState auth_state;
  FingerprintAuthBlockState fingerprint_auth_state;
  fingerprint_auth_state.template_id = record_id;
  auth_state.state = fingerprint_auth_state;
  return auth_state;
}

AuthBlockState GetFingerprintStateWithFakeLabel() {
  AuthBlockState auth_state;
  FingerprintAuthBlockState fingerprint_auth_state;
  fingerprint_auth_state.gsc_secret_label = kFakeCredLabel;
  auth_state.state = fingerprint_auth_state;
  return auth_state;
}

class FingerprintAuthBlockTest : public ::testing::Test {
 public:
  void SetUp() override {
    auto mock_processor =
        std::make_unique<StrictMock<MockBiometricsCommandProcessor>>();
    mock_processor_ = mock_processor.get();
    EXPECT_CALL(*mock_processor_, SetEnrollScanDoneCallback(_))
        .WillOnce(SaveArg<0>(&enroll_callback_));
    EXPECT_CALL(*mock_processor_, SetAuthScanDoneCallback(_))
        .WillOnce(SaveArg<0>(&auth_callback_));
    EXPECT_CALL(*mock_processor_, SetSessionFailedCallback);
    bio_service_ = std::make_unique<BiometricsAuthBlockService>(
        std::move(mock_processor), /*enroll_signal_sender=*/base::DoNothing(),
        /*auth_signal_sender=*/base::DoNothing());
    auth_block_ = std::make_unique<FingerprintAuthBlock>(&mock_le_manager_,
                                                         bio_service_.get());
  }

 protected:
  const error::CryptohomeError::ErrorLocationPair kErrorLocationPlaceholder =
      error::CryptohomeError::ErrorLocationPair(
          static_cast<::cryptohome::error::CryptohomeError::ErrorLocation>(1),
          "Testing1");
  const ObfuscatedUsername kFakeAccountId{"account_id"};
  const brillo::Blob kFakeAuthNonce{32, 100};

  // We can just emit empty proto message here as this test only relies on the
  // nonce.
  void EmitEnrollEvent(std::optional<brillo::Blob> nonce) {
    enroll_callback_.Run(AuthEnrollmentProgress{}, std::move(nonce));
  }

  void EmitAuthEvent(brillo::Blob nonce) {
    auth_callback_.Run(AuthScanDone{}, std::move(nonce));
  }

  void StartEnrollSession(std::unique_ptr<PreparedAuthFactorToken>& ret_token) {
    EXPECT_CALL(*mock_processor_, StartEnrollSession(_))
        .WillOnce([](auto&& callback) { std::move(callback).Run(true); });

    TestFuture<CryptohomeStatusOr<std::unique_ptr<PreparedAuthFactorToken>>>
        result;
    bio_service_->StartEnrollSession(AuthFactorType::kFingerprint,
                                     kFakeAccountId, result.GetCallback());
    ASSERT_TRUE(result.IsReady());
    CryptohomeStatusOr<std::unique_ptr<PreparedAuthFactorToken>> token =
        result.Take();
    ASSERT_TRUE(token.ok());
    EmitEnrollEvent(kFakeAuthNonce);
    ret_token = *std::move(token);

    EXPECT_CALL(*mock_processor_, EndEnrollSession);
  }

  void StartAuthenticateSession(
      std::unique_ptr<PreparedAuthFactorToken>& ret_token) {
    EXPECT_CALL(*mock_processor_, StartAuthenticateSession(kFakeAccountId, _))
        .WillRepeatedly(
            [](auto&&, auto&& callback) { std::move(callback).Run(true); });

    TestFuture<CryptohomeStatusOr<std::unique_ptr<PreparedAuthFactorToken>>>
        result;
    bio_service_->StartAuthenticateSession(
        AuthFactorType::kFingerprint, kFakeAccountId, result.GetCallback());
    ASSERT_TRUE(result.IsReady());
    CryptohomeStatusOr<std::unique_ptr<PreparedAuthFactorToken>> token =
        result.Take();
    ASSERT_TRUE(token.ok());
    EmitAuthEvent(kFakeAuthNonce);
    ret_token = *std::move(token);

    EXPECT_CALL(*mock_processor_, EndAuthenticateSession).Times(AnyNumber());
  }

  base::test::TaskEnvironment task_environment_;

  StrictMock<MockLECredentialManager> mock_le_manager_;
  StrictMock<MockBiometricsCommandProcessor>* mock_processor_;
  base::RepeatingCallback<void(AuthEnrollmentProgress,
                               std::optional<brillo::Blob>)>
      enroll_callback_;
  base::RepeatingCallback<void(AuthScanDone, brillo::Blob)> auth_callback_;
  std::unique_ptr<BiometricsAuthBlockService> bio_service_;
  std::unique_ptr<FingerprintAuthBlock> auth_block_;
};

TEST_F(FingerprintAuthBlockTest, CreateSuccess) {
  const brillo::SecureBlob kFakeResetSecret(32, 1);
  const brillo::SecureBlob kFakeAuthSecret(32, 3), kFakeAuthPin(32, 4);
  const brillo::Blob kFakeGscNonce(32, 1), kFakeLabelSeed(32, 2);
  const brillo::Blob kFakeGscIv(16, 1);
  const AuthInput kFakeAuthInput{.obfuscated_username = kFakeAccountId,
                                 .reset_secret = kFakeResetSecret,
                                 .rate_limiter_label = kFakeRateLimiterLabel};

  std::unique_ptr<PreparedAuthFactorToken> token;
  StartEnrollSession(token);
  ASSERT_NE(token, nullptr);

  EXPECT_CALL(
      mock_le_manager_,
      StartBiometricsAuth(kFingerprintAuthChannel, kFakeRateLimiterLabel,
                          BlobToSecureBlob(kFakeAuthNonce)))
      .WillOnce(ReturnValue(LECredentialManager::StartBiometricsAuthReply{
          .server_nonce = BlobToSecureBlob(kFakeGscNonce),
          .iv = BlobToSecureBlob(kFakeGscIv),
          .encrypted_he_secret = BlobToSecureBlob(kFakeLabelSeed)}));
  EXPECT_CALL(
      *mock_processor_,
      CreateCredential(
          kFakeAccountId,
          AllOf(Field(&OperationInput::nonce, kFakeGscNonce),
                Field(&OperationInput::encrypted_label_seed, kFakeLabelSeed),
                Field(&OperationInput::iv, kFakeGscIv)),
          _))
      .WillOnce([&](auto&&, auto&&, auto&& callback) {
        std::move(callback).Run(OperationOutput{
            .record_id = kFakeRecordId,
            .auth_secret = kFakeAuthSecret,
            .auth_pin = kFakeAuthPin,
        });
      });
  EXPECT_CALL(mock_le_manager_,
              InsertCredential(_, kFakeAuthPin, _, kFakeResetSecret, _, _, _))
      .WillOnce(DoAll(SetArgPointee<6>(kFakeCredLabel),
                      ReturnOk<CryptohomeLECredError>()));

  CreateTestFuture result;
  auth_block_->Create(kFakeAuthInput, result.GetCallback());

  ASSERT_TRUE(result.IsReady());
  auto [status, key_blobs, auth_state] = result.Take();
  ASSERT_THAT(status, IsOk());
  ASSERT_NE(key_blobs, nullptr);
  ASSERT_TRUE(key_blobs->vkk_key.has_value());
  EXPECT_THAT(key_blobs->vkk_key.value(), SizeIs(32));
  ASSERT_TRUE(key_blobs->reset_secret.has_value());
  EXPECT_EQ(key_blobs->reset_secret.value(), kFakeResetSecret);
  EXPECT_FALSE(key_blobs->rate_limiter_label.has_value());
  ASSERT_NE(auth_state, nullptr);
  ASSERT_TRUE(
      std::holds_alternative<FingerprintAuthBlockState>(auth_state->state));
  auto& state = std::get<FingerprintAuthBlockState>(auth_state->state);
  const std::string record_id = kFakeRecordId;
  EXPECT_EQ(state.template_id, record_id);
  EXPECT_EQ(state.gsc_secret_label, kFakeCredLabel);
}

TEST_F(FingerprintAuthBlockTest, CreateNoUsername) {
  CreateTestFuture result;
  auth_block_->Create(AuthInput{}, result.GetCallback());

  ASSERT_TRUE(result.IsReady());
  auto [status, key_blobs, auth_state] = result.Take();
  EXPECT_TRUE(
      ContainsActionInStack(status, ErrorAction::kDevCheckUnexpectedState));
  EXPECT_EQ(key_blobs, nullptr);
  EXPECT_EQ(auth_state, nullptr);
}

TEST_F(FingerprintAuthBlockTest, CreateNoSession) {
  const brillo::SecureBlob kFakeResetSecret(32, 1);
  const AuthInput kFakeAuthInput{.obfuscated_username = kFakeAccountId,
                                 .reset_secret = kFakeResetSecret,
                                 .rate_limiter_label = kFakeRateLimiterLabel};

  CreateTestFuture result;
  auth_block_->Create(kFakeAuthInput, result.GetCallback());

  ASSERT_TRUE(result.IsReady());
  auto [status, key_blobs, auth_state] = result.Take();
  EXPECT_TRUE(
      ContainsActionInStack(status, ErrorAction::kDevCheckUnexpectedState));
  EXPECT_EQ(key_blobs, nullptr);
  EXPECT_EQ(auth_state, nullptr);
}

TEST_F(FingerprintAuthBlockTest, CreateStartBioAuthFailed) {
  const brillo::SecureBlob kFakeResetSecret(32, 1);
  const AuthInput kFakeAuthInput{.obfuscated_username = kFakeAccountId,
                                 .reset_secret = kFakeResetSecret,
                                 .rate_limiter_label = kFakeRateLimiterLabel};

  std::unique_ptr<PreparedAuthFactorToken> token;
  StartEnrollSession(token);
  ASSERT_NE(token, nullptr);

  EXPECT_CALL(mock_le_manager_, StartBiometricsAuth)
      .WillOnce([this](auto&&, auto&&, auto&&) {
        return MakeStatus<CryptohomeLECredError>(
            kErrorLocationPlaceholder,
            ErrorActionSet({ErrorAction::kLeLockedOut}),
            LECredError::LE_CRED_ERROR_TOO_MANY_ATTEMPTS);
      });

  CreateTestFuture result;
  auth_block_->Create(kFakeAuthInput, result.GetCallback());

  ASSERT_TRUE(result.IsReady());
  auto [status, key_blobs, auth_state] = result.Take();
  EXPECT_TRUE(ContainsActionInStack(status, ErrorAction::kLeLockedOut));
  EXPECT_EQ(key_blobs, nullptr);
  EXPECT_EQ(auth_state, nullptr);
}

TEST_F(FingerprintAuthBlockTest, CreateCreateCredentialFailed) {
  const brillo::SecureBlob kFakeResetSecret(32, 1);
  const brillo::Blob kFakeGscNonce(32, 1), kFakeLabelSeed(32, 2);
  const brillo::Blob kFakeGscIv(16, 1);
  const AuthInput kFakeAuthInput{.obfuscated_username = kFakeAccountId,
                                 .reset_secret = kFakeResetSecret,
                                 .rate_limiter_label = kFakeRateLimiterLabel};

  std::unique_ptr<PreparedAuthFactorToken> token;
  StartEnrollSession(token);
  ASSERT_NE(token, nullptr);

  EXPECT_CALL(
      mock_le_manager_,
      StartBiometricsAuth(kFingerprintAuthChannel, kFakeRateLimiterLabel,
                          BlobToSecureBlob(kFakeAuthNonce)))
      .WillOnce(ReturnValue(LECredentialManager::StartBiometricsAuthReply{
          .server_nonce = BlobToSecureBlob(kFakeGscNonce),
          .iv = BlobToSecureBlob(kFakeGscIv),
          .encrypted_he_secret = BlobToSecureBlob(kFakeLabelSeed)}));
  EXPECT_CALL(*mock_processor_, CreateCredential(_, _, _))
      .WillOnce([&](auto&&, auto&&, auto&& callback) {
        std::move(callback).Run(MakeStatus<CryptohomeError>(
            kErrorLocationPlaceholder,
            ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
            user_data_auth::CRYPTOHOME_ERROR_NOT_IMPLEMENTED));
      });

  CreateTestFuture result;
  auth_block_->Create(kFakeAuthInput, result.GetCallback());

  ASSERT_TRUE(result.IsReady());
  auto [status, key_blobs, auth_state] = result.Take();
  EXPECT_TRUE(
      ContainsActionInStack(status, ErrorAction::kDevCheckUnexpectedState));
  EXPECT_EQ(key_blobs, nullptr);
  EXPECT_EQ(auth_state, nullptr);
}

TEST_F(FingerprintAuthBlockTest, CreateInsertCredentialFailed) {
  const brillo::SecureBlob kFakeResetSecret(32, 1);
  const brillo::SecureBlob kFakeAuthSecret(32, 3), kFakeAuthPin(32, 4);
  const brillo::Blob kFakeGscNonce(32, 1), kFakeLabelSeed(32, 2);
  const brillo::Blob kFakeGscIv(16, 1);
  const AuthInput kFakeAuthInput{.obfuscated_username = kFakeAccountId,
                                 .reset_secret = kFakeResetSecret,
                                 .rate_limiter_label = kFakeRateLimiterLabel};

  std::unique_ptr<PreparedAuthFactorToken> token;
  StartEnrollSession(token);
  ASSERT_NE(token, nullptr);

  EXPECT_CALL(
      mock_le_manager_,
      StartBiometricsAuth(kFingerprintAuthChannel, kFakeRateLimiterLabel,
                          BlobToSecureBlob(kFakeAuthNonce)))
      .WillOnce(ReturnValue(LECredentialManager::StartBiometricsAuthReply{
          .server_nonce = BlobToSecureBlob(kFakeGscNonce),
          .iv = BlobToSecureBlob(kFakeGscIv),
          .encrypted_he_secret = BlobToSecureBlob(kFakeLabelSeed)}));
  EXPECT_CALL(
      *mock_processor_,
      CreateCredential(
          kFakeAccountId,
          AllOf(Field(&OperationInput::nonce, kFakeGscNonce),
                Field(&OperationInput::encrypted_label_seed, kFakeLabelSeed),
                Field(&OperationInput::iv, kFakeGscIv)),
          _))
      .WillOnce([&](auto&&, auto&&, auto&& callback) {
        std::move(callback).Run(OperationOutput{
            .record_id = kFakeRecordId,
            .auth_secret = kFakeAuthSecret,
            .auth_pin = kFakeAuthPin,
        });
      });
  EXPECT_CALL(mock_le_manager_, InsertCredential)
      .WillOnce([this](auto&&, auto&&, auto&&, auto&&, auto&&, auto&&, auto&&) {
        return MakeStatus<CryptohomeLECredError>(
            kErrorLocationPlaceholder,
            ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
            LECredError::LE_CRED_ERROR_HASH_TREE);
      });

  CreateTestFuture result;
  auth_block_->Create(kFakeAuthInput, result.GetCallback());

  ASSERT_TRUE(result.IsReady());
  auto [status, key_blobs, auth_state] = result.Take();
  EXPECT_TRUE(
      ContainsActionInStack(status, ErrorAction::kDevCheckUnexpectedState));
  EXPECT_EQ(key_blobs, nullptr);
  EXPECT_EQ(auth_state, nullptr);
}

TEST_F(FingerprintAuthBlockTest, CreateWithNoLimiterSuccess) {
  const brillo::SecureBlob kFakeAuthSecret(32, 3), kFakeAuthPin(32, 4);
  const brillo::Blob kFakeGscNonce(32, 1), kFakeLabelSeed(32, 2);
  const brillo::Blob kFakeGscIv(16, 1);
  const AuthInput kFakeAuthInput{.obfuscated_username = kFakeAccountId};

  std::unique_ptr<PreparedAuthFactorToken> token;
  StartEnrollSession(token);
  ASSERT_NE(token, nullptr);

  brillo::SecureBlob reset_secret;
  EXPECT_CALL(mock_le_manager_,
              InsertRateLimiter(kFingerprintAuthChannel, _, _, _, _, _))
      .WillOnce(DoAll(SaveArg<2>(&reset_secret),
                      SetArgPointee<5>(kFakeRateLimiterLabel),
                      ReturnOk<CryptohomeLECredError>()));
  EXPECT_CALL(
      mock_le_manager_,
      StartBiometricsAuth(kFingerprintAuthChannel, kFakeRateLimiterLabel,
                          BlobToSecureBlob(kFakeAuthNonce)))
      .WillOnce(ReturnValue(LECredentialManager::StartBiometricsAuthReply{
          .server_nonce = BlobToSecureBlob(kFakeGscNonce),
          .iv = BlobToSecureBlob(kFakeGscIv),
          .encrypted_he_secret = BlobToSecureBlob(kFakeLabelSeed)}));
  EXPECT_CALL(
      *mock_processor_,
      CreateCredential(
          kFakeAccountId,
          AllOf(Field(&OperationInput::nonce, kFakeGscNonce),
                Field(&OperationInput::encrypted_label_seed, kFakeLabelSeed),
                Field(&OperationInput::iv, kFakeGscIv)),
          _))
      .WillOnce([&](auto&&, auto&&, auto&& callback) {
        std::move(callback).Run(OperationOutput{
            .record_id = kFakeRecordId,
            .auth_secret = kFakeAuthSecret,
            .auth_pin = kFakeAuthPin,
        });
      });
  EXPECT_CALL(mock_le_manager_,
              InsertCredential(_, kFakeAuthPin, _, _, _, _, _))
      .WillOnce([&](auto&&, auto&&, auto&&, auto&& secret, auto&&, auto&&,
                    auto&& label) {
        EXPECT_EQ(secret, reset_secret);
        *label = kFakeCredLabel;
        return OkStatus<CryptohomeLECredError>();
      });

  CreateTestFuture result;
  auth_block_->Create(kFakeAuthInput, result.GetCallback());

  ASSERT_TRUE(result.IsReady());
  auto [status, key_blobs, auth_state] = result.Take();
  ASSERT_THAT(status, IsOk());
  ASSERT_NE(key_blobs, nullptr);
  ASSERT_TRUE(key_blobs->vkk_key.has_value());
  EXPECT_THAT(key_blobs->vkk_key.value(), SizeIs(32));
  ASSERT_TRUE(key_blobs->reset_secret.has_value());
  EXPECT_EQ(key_blobs->reset_secret.value(), reset_secret);
  ASSERT_TRUE(key_blobs->rate_limiter_label.has_value());
  EXPECT_EQ(key_blobs->rate_limiter_label, kFakeRateLimiterLabel);
  ASSERT_NE(auth_state, nullptr);
  ASSERT_TRUE(
      std::holds_alternative<FingerprintAuthBlockState>(auth_state->state));
  auto& state = std::get<FingerprintAuthBlockState>(auth_state->state);
  const std::string record_id = kFakeRecordId;
  EXPECT_EQ(state.template_id, record_id);
  EXPECT_EQ(state.gsc_secret_label, kFakeCredLabel);
}

TEST_F(FingerprintAuthBlockTest, CreateLimiterFailed) {
  const AuthInput kFakeAuthInput{.obfuscated_username = kFakeAccountId};

  EXPECT_CALL(mock_le_manager_, InsertRateLimiter)
      .WillOnce([this](auto&&, auto&&, auto&&, auto&&, auto&&, auto&&) {
        return MakeStatus<CryptohomeLECredError>(
            kErrorLocationPlaceholder,
            ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
            LECredError::LE_CRED_ERROR_HASH_TREE);
      });

  CreateTestFuture result;
  auth_block_->Create(kFakeAuthInput, result.GetCallback());

  ASSERT_TRUE(result.IsReady());
  auto [status, key_blobs, auth_state] = result.Take();
  EXPECT_TRUE(
      ContainsActionInStack(status, ErrorAction::kDevCheckUnexpectedState));
  EXPECT_EQ(key_blobs, nullptr);
  EXPECT_EQ(auth_state, nullptr);
}

// If AuthInput contains rate-limiter label but no reset secret, the request
// should fail with InvalidArgument.
TEST_F(FingerprintAuthBlockTest, CreateNoResetSecretFailed) {
  const AuthInput kFakeAuthInput{.obfuscated_username = kFakeAccountId,
                                 .rate_limiter_label = kFakeRateLimiterLabel};

  CreateTestFuture result;
  auth_block_->Create(kFakeAuthInput, result.GetCallback());

  ASSERT_TRUE(result.IsReady());
  auto [status, key_blobs, auth_state] = result.Take();
  EXPECT_TRUE(
      ContainsActionInStack(status, ErrorAction::kDevCheckUnexpectedState));
  EXPECT_EQ(status->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(key_blobs, nullptr);
  EXPECT_EQ(auth_state, nullptr);
}

TEST_F(FingerprintAuthBlockTest, SelectFactorSuccess) {
  const brillo::SecureBlob kFakeAuthSecret(32, 1), kFakeAuthPin(32, 2);
  const brillo::Blob kFakeGscNonce(32, 1), kFakeLabelSeed(32, 2);
  const brillo::Blob kFakeGscIv(16, 1);
  const AuthInput kFakeAuthInput{.rate_limiter_label = kFakeRateLimiterLabel};
  const std::vector<AuthFactor> kFakeAuthFactors{
      AuthFactor(AuthFactorType::kFingerprint, kFakeAuthFactorLabel1,
                 AuthFactorMetadata{},
                 GetFingerprintStateWithRecordId(kFakeRecordId)),
      AuthFactor(AuthFactorType::kFingerprint, kFakeAuthFactorLabel2,
                 AuthFactorMetadata{},
                 GetFingerprintStateWithRecordId(kFakeRecordId2))};

  std::unique_ptr<PreparedAuthFactorToken> token;
  StartAuthenticateSession(token);
  ASSERT_NE(token, nullptr);

  EXPECT_CALL(
      mock_le_manager_,
      StartBiometricsAuth(kFingerprintAuthChannel, kFakeRateLimiterLabel,
                          BlobToSecureBlob(kFakeAuthNonce)))
      .WillOnce(ReturnValue(LECredentialManager::StartBiometricsAuthReply{
          .server_nonce = BlobToSecureBlob(kFakeGscNonce),
          .iv = BlobToSecureBlob(kFakeGscIv),
          .encrypted_he_secret = BlobToSecureBlob(kFakeLabelSeed)}));
  EXPECT_CALL(
      *mock_processor_,
      MatchCredential(
          AllOf(Field(&OperationInput::nonce, kFakeGscNonce),
                Field(&OperationInput::encrypted_label_seed, kFakeLabelSeed),
                Field(&OperationInput::iv, kFakeGscIv)),
          _))
      .WillOnce([&](auto&&, auto&& callback) {
        std::move(callback).Run(OperationOutput{
            .record_id = kFakeRecordId,
            .auth_secret = kFakeAuthSecret,
            .auth_pin = kFakeAuthPin,
        });
      });

  SelectFactorTestFuture result;
  auth_block_->SelectFactor(kFakeAuthInput, kFakeAuthFactors,
                            result.GetCallback());

  ASSERT_TRUE(result.IsReady());
  auto [status, auth_input, auth_factor] = result.Take();
  ASSERT_THAT(status, IsOk());
  ASSERT_TRUE(auth_input.has_value());
  EXPECT_EQ(auth_input->user_input, kFakeAuthPin);
  ASSERT_TRUE(auth_input->fingerprint_auth_input.has_value());
  ASSERT_TRUE(auth_input->fingerprint_auth_input->auth_secret.has_value());
  EXPECT_EQ(auth_input->fingerprint_auth_input->auth_secret, kFakeAuthSecret);
  ASSERT_TRUE(auth_factor.has_value());
  const AuthBlockState& auth_state = auth_factor->auth_block_state();
  ASSERT_TRUE(
      std::holds_alternative<FingerprintAuthBlockState>(auth_state.state));
  auto& state = std::get<FingerprintAuthBlockState>(auth_state.state);
  EXPECT_EQ(state.template_id, std::string(kFakeRecordId));
}

TEST_F(FingerprintAuthBlockTest, SelectFactorNoLabel) {
  const AuthInput kFakeAuthInput{};
  const std::vector<AuthFactor> kFakeAuthFactors{};

  SelectFactorTestFuture result;
  auth_block_->SelectFactor(kFakeAuthInput, kFakeAuthFactors,
                            result.GetCallback());

  ASSERT_TRUE(result.IsReady());
  auto [status, auth_input, auth_factor] = result.Take();
  EXPECT_TRUE(
      ContainsActionInStack(status, ErrorAction::kDevCheckUnexpectedState));
  EXPECT_FALSE(auth_input.has_value());
  EXPECT_FALSE(auth_factor.has_value());
}

TEST_F(FingerprintAuthBlockTest, SelectFactorNoSession) {
  const AuthInput kFakeAuthInput{.rate_limiter_label = kFakeRateLimiterLabel};
  const std::vector<AuthFactor> kFakeAuthFactors{
      AuthFactor(AuthFactorType::kFingerprint, kFakeAuthFactorLabel1,
                 AuthFactorMetadata{},
                 GetFingerprintStateWithRecordId(kFakeRecordId)),
      AuthFactor(AuthFactorType::kFingerprint, kFakeAuthFactorLabel2,
                 AuthFactorMetadata{},
                 GetFingerprintStateWithRecordId(kFakeRecordId2))};

  SelectFactorTestFuture result;
  auth_block_->SelectFactor(kFakeAuthInput, kFakeAuthFactors,
                            result.GetCallback());

  ASSERT_TRUE(result.IsReady());
  auto [status, auth_input, auth_factor] = result.Take();
  EXPECT_TRUE(
      ContainsActionInStack(status, ErrorAction::kDevCheckUnexpectedState));
  EXPECT_FALSE(auth_input.has_value());
  EXPECT_FALSE(auth_factor.has_value());
}

TEST_F(FingerprintAuthBlockTest, SelectFactorStartBioAuthFailed) {
  const AuthInput kFakeAuthInput{.rate_limiter_label = kFakeRateLimiterLabel};
  const std::vector<AuthFactor> kFakeAuthFactors{
      AuthFactor(AuthFactorType::kFingerprint, kFakeAuthFactorLabel1,
                 AuthFactorMetadata{},
                 GetFingerprintStateWithRecordId(kFakeRecordId)),
      AuthFactor(AuthFactorType::kFingerprint, kFakeAuthFactorLabel2,
                 AuthFactorMetadata{},
                 GetFingerprintStateWithRecordId(kFakeRecordId2))};

  std::unique_ptr<PreparedAuthFactorToken> token;
  StartAuthenticateSession(token);
  ASSERT_NE(token, nullptr);

  EXPECT_CALL(mock_le_manager_, StartBiometricsAuth)
      .WillOnce([this](auto&&, auto&&, auto&&) {
        return MakeStatus<CryptohomeLECredError>(
            kErrorLocationPlaceholder,
            ErrorActionSet({ErrorAction::kLeLockedOut}),
            LECredError::LE_CRED_ERROR_TOO_MANY_ATTEMPTS);
      });

  SelectFactorTestFuture result;
  auth_block_->SelectFactor(kFakeAuthInput, kFakeAuthFactors,
                            result.GetCallback());

  ASSERT_TRUE(result.IsReady());
  auto [status, auth_input, auth_factor] = result.Take();
  EXPECT_TRUE(ContainsActionInStack(status, ErrorAction::kLeLockedOut));
  EXPECT_FALSE(auth_input.has_value());
  EXPECT_FALSE(auth_factor.has_value());
}

TEST_F(FingerprintAuthBlockTest, SelectFactorMatchFailed) {
  const brillo::Blob kFakeGscNonce(32, 1), kFakeLabelSeed(32, 2);
  const brillo::Blob kFakeGscIv(16, 1);
  const AuthInput kFakeAuthInput{.rate_limiter_label = kFakeRateLimiterLabel};
  const std::vector<AuthFactor> kFakeAuthFactors{
      AuthFactor(AuthFactorType::kFingerprint, kFakeAuthFactorLabel1,
                 AuthFactorMetadata{},
                 GetFingerprintStateWithRecordId(kFakeRecordId)),
      AuthFactor(AuthFactorType::kFingerprint, kFakeAuthFactorLabel2,
                 AuthFactorMetadata{},
                 GetFingerprintStateWithRecordId(kFakeRecordId2))};

  std::unique_ptr<PreparedAuthFactorToken> token;
  StartAuthenticateSession(token);
  ASSERT_NE(token, nullptr);

  EXPECT_CALL(
      mock_le_manager_,
      StartBiometricsAuth(kFingerprintAuthChannel, kFakeRateLimiterLabel,
                          BlobToSecureBlob(kFakeAuthNonce)))
      .WillOnce(ReturnValue(LECredentialManager::StartBiometricsAuthReply{
          .server_nonce = BlobToSecureBlob(kFakeGscNonce),
          .iv = BlobToSecureBlob(kFakeGscIv),
          .encrypted_he_secret = BlobToSecureBlob(kFakeLabelSeed)}));
  EXPECT_CALL(*mock_processor_, MatchCredential(_, _))
      .WillOnce([&](auto&&, auto&& callback) {
        std::move(callback).Run(MakeStatus<CryptohomeError>(
            kErrorLocationPlaceholder,
            ErrorActionSet({ErrorAction::kIncorrectAuth}),
            user_data_auth::CRYPTOHOME_ERROR_NOT_IMPLEMENTED));
      });

  SelectFactorTestFuture result;
  auth_block_->SelectFactor(kFakeAuthInput, kFakeAuthFactors,
                            result.GetCallback());

  ASSERT_TRUE(result.IsReady());
  auto [status, auth_input, auth_factor] = result.Take();
  EXPECT_TRUE(ContainsActionInStack(status, ErrorAction::kIncorrectAuth));
  EXPECT_FALSE(auth_input.has_value());
  EXPECT_FALSE(auth_factor.has_value());
}

TEST_F(FingerprintAuthBlockTest, SelectFactorAuthFactorNotInList) {
  const brillo::SecureBlob kFakeAuthSecret(32, 1), kFakeAuthPin(32, 2);
  const brillo::Blob kFakeGscNonce(32, 1), kFakeLabelSeed(32, 2);
  const brillo::Blob kFakeGscIv(16, 1);
  const AuthInput kFakeAuthInput{.rate_limiter_label = kFakeRateLimiterLabel};
  const std::vector<AuthFactor> kFakeAuthFactors{
      AuthFactor(AuthFactorType::kFingerprint, kFakeAuthFactorLabel1,
                 AuthFactorMetadata{},
                 GetFingerprintStateWithRecordId(kFakeRecordId)),
      AuthFactor(AuthFactorType::kFingerprint, kFakeAuthFactorLabel2,
                 AuthFactorMetadata{},
                 GetFingerprintStateWithRecordId(kFakeRecordId2))};

  std::unique_ptr<PreparedAuthFactorToken> token;
  StartAuthenticateSession(token);
  ASSERT_NE(token, nullptr);

  EXPECT_CALL(
      mock_le_manager_,
      StartBiometricsAuth(kFingerprintAuthChannel, kFakeRateLimiterLabel,
                          BlobToSecureBlob(kFakeAuthNonce)))
      .WillOnce(ReturnValue(LECredentialManager::StartBiometricsAuthReply{
          .server_nonce = BlobToSecureBlob(kFakeGscNonce),
          .iv = BlobToSecureBlob(kFakeGscIv),
          .encrypted_he_secret = BlobToSecureBlob(kFakeLabelSeed)}));
  EXPECT_CALL(
      *mock_processor_,
      MatchCredential(
          AllOf(Field(&OperationInput::nonce, kFakeGscNonce),
                Field(&OperationInput::encrypted_label_seed, kFakeLabelSeed),
                Field(&OperationInput::iv, kFakeGscIv)),
          _))
      .WillOnce([&](auto&&, auto&& callback) {
        std::move(callback).Run(OperationOutput{
            .record_id = "unknown_record",
            .auth_secret = kFakeAuthSecret,
            .auth_pin = kFakeAuthPin,
        });
      });

  SelectFactorTestFuture result;
  auth_block_->SelectFactor(kFakeAuthInput, kFakeAuthFactors,
                            result.GetCallback());

  ASSERT_TRUE(result.IsReady());
  auto [status, auth_input, auth_factor] = result.Take();
  EXPECT_TRUE(
      ContainsActionInStack(status, ErrorAction::kDevCheckUnexpectedState));
  EXPECT_FALSE(auth_input.has_value());
  EXPECT_FALSE(auth_factor.has_value());
}

TEST_F(FingerprintAuthBlockTest, DeriveSuccess) {
  const brillo::SecureBlob kFakeAuthSecret(32, 1), kFakeAuthPin(32, 2);
  const brillo::SecureBlob kFakeGscSecret(32, 3);
  const AuthInput kFakeAuthInput{
      .user_input = kFakeAuthPin,
      .fingerprint_auth_input =
          FingerprintAuthInput{
              .auth_secret = kFakeAuthSecret,
          },
  };
  const AuthBlockState kFakeAuthBlockState = GetFingerprintStateWithFakeLabel();

  EXPECT_CALL(mock_le_manager_,
              CheckCredential(kFakeCredLabel, kFakeAuthPin, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(kFakeGscSecret),
                      ReturnOk<CryptohomeLECredError>()));

  DeriveTestFuture result;
  auth_block_->Derive(kFakeAuthInput, kFakeAuthBlockState,
                      result.GetCallback());

  ASSERT_TRUE(result.IsReady());
  auto [status, key_blobs] = result.Take();
  ASSERT_THAT(status, IsOk());
  ASSERT_NE(key_blobs, nullptr);
  ASSERT_TRUE(key_blobs->vkk_key.has_value());
  EXPECT_THAT(key_blobs->vkk_key.value(), SizeIs(32));
}

TEST_F(FingerprintAuthBlockTest, DeriveInvalidAuthInput) {
  const brillo::SecureBlob kFakeAuthPin(32, 1), kFakeGscSecret(32, 2);
  const AuthInput kFakeAuthInput{.user_input = kFakeAuthPin};
  const AuthBlockState kFakeAuthBlockState = GetFingerprintStateWithFakeLabel();

  DeriveTestFuture result;
  auth_block_->Derive(kFakeAuthInput, kFakeAuthBlockState,
                      result.GetCallback());

  ASSERT_TRUE(result.IsReady());
  auto [status, key_blobs] = result.Take();
  EXPECT_TRUE(
      ContainsActionInStack(status, ErrorAction::kDevCheckUnexpectedState));
  EXPECT_EQ(key_blobs, nullptr);
}

TEST_F(FingerprintAuthBlockTest, DeriveCheckCredentialFailed) {
  const brillo::SecureBlob kFakeAuthSecret(32, 1), kFakeAuthPin(32, 2);
  const brillo::SecureBlob kFakeGscSecret(32, 3);
  const AuthInput kFakeAuthInput{
      .user_input = kFakeAuthPin,
      .fingerprint_auth_input =
          FingerprintAuthInput{
              .auth_secret = kFakeAuthSecret,
          },
  };
  const AuthBlockState kFakeAuthBlockState = GetFingerprintStateWithFakeLabel();

  EXPECT_CALL(mock_le_manager_, CheckCredential)
      .WillOnce([this](auto&&, auto&&, auto&&, auto&&) {
        return MakeStatus<CryptohomeLECredError>(
            kErrorLocationPlaceholder,
            ErrorActionSet({ErrorAction::kLeLockedOut}),
            LECredError::LE_CRED_ERROR_TOO_MANY_ATTEMPTS);
      });

  DeriveTestFuture result;
  auth_block_->Derive(kFakeAuthInput, kFakeAuthBlockState,
                      result.GetCallback());

  ASSERT_TRUE(result.IsReady());
  auto [status, key_blobs] = result.Take();
  EXPECT_TRUE(
      ContainsActionInStack(status, ErrorAction::kDevCheckUnexpectedState));
  EXPECT_EQ(key_blobs, nullptr);
}

}  // namespace
}  // namespace cryptohome
