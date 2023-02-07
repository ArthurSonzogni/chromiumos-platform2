// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_blocks/biometrics_auth_block_service.h"

#include <utility>

#include <base/callback.h>
#include <base/test/repeating_test_future.h>
#include <base/task/sequenced_task_runner.h>
#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <base/threading/sequenced_task_runner_handle.h>
#include <gtest/gtest.h>
#include <libhwsec-foundation/error/testing_helper.h>

#include "cryptohome/auth_blocks/mock_biometrics_command_processor.h"
#include "cryptohome/auth_factor/auth_factor_type.h"

namespace cryptohome {
namespace {

using base::test::RepeatingTestFuture;
using base::test::TestFuture;
using hwsec_foundation::error::testing::IsOk;
using hwsec_foundation::error::testing::IsOkAnd;

using ::testing::_;
using ::testing::InSequence;
using ::testing::SaveArg;

using user_data_auth::AuthEnrollmentProgress;
using user_data_auth::FingerprintScanResult;

// Compares protobuf message by serialization.
MATCHER_P(ProtoEq, proto, "") {
  // Make sure given proto types are same.
  using ArgType = typename std::remove_cv<
      typename std::remove_reference<decltype(arg)>::type>::type;
  using ProtoType = typename std::remove_cv<
      typename std::remove_reference<decltype(proto)>::type>::type;
  static_assert(std::is_same<ArgType, ProtoType>::value, "Proto type mismatch");

  return arg.SerializeAsString() == proto.SerializeAsString();
}

// Compares OperationInput/Output structs.
MATCHER_P(OperationInputEq, input, "") {
  return arg.nonce == input.nonce &&
         arg.encrypted_label_seed == input.encrypted_label_seed &&
         arg.iv == input.iv;
}

MATCHER_P(OperationOutputEq, output, "") {
  return arg.record_id == output.record_id &&
         arg.auth_secret == output.auth_secret &&
         arg.auth_pin == output.auth_pin;
}

// Functors for saving on_done callbacks of biometrics command processor methods
// into a given argument. Useful for mocking out the biometrics command
// processor methods.
struct SaveStartEnrollSessionCallback {
  void operator()(base::OnceCallback<void(bool)> callback) {
    *captured_callback = std::move(callback);
  }
  base::OnceCallback<void(bool)>* captured_callback;
};

struct SaveCreateCredentialCallback {
  void operator()(ObfuscatedUsername,
                  BiometricsCommandProcessor::OperationInput,
                  BiometricsCommandProcessor::OperationCallback callback) {
    *captured_callback = std::move(callback);
  }
  BiometricsCommandProcessor::OperationCallback* captured_callback;
};

AuthEnrollmentProgress ConstructAuthEnrollmentProgress(
    FingerprintScanResult scan_result, int percent_complete) {
  AuthEnrollmentProgress ret;
  ret.mutable_scan_result()->set_fingerprint_result(scan_result);
  ret.set_done(percent_complete == 100);
  ret.mutable_fingerprint_progress()->set_percent_complete(percent_complete);
  return ret;
}

// Base test fixture which sets up the task environment.
class BaseTestFixture : public ::testing::Test {
 protected:
  base::test::SingleThreadTaskEnvironment task_environment_ = {
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  scoped_refptr<base::SequencedTaskRunner> task_runner_ =
      base::SequencedTaskRunnerHandle::Get();
};

class BiometricsAuthBlockServiceTest : public BaseTestFixture {
 public:
  void SetUp() override {
    auto mock_processor = std::make_unique<MockBiometricsCommandProcessor>();
    mock_processor_ = mock_processor.get();
    EXPECT_CALL(*mock_processor_, SetEnrollScanDoneCallback(_))
        .WillOnce(SaveArg<0>(&enroll_callback_));
    service_ = std::make_unique<BiometricsAuthBlockService>(
        std::move(mock_processor), enroll_signals_.GetCallback(),
        base::DoNothing());
  }

 protected:
  const ObfuscatedUsername kFakeUserId{"fake"};

  void EmitEnrollEvent(user_data_auth::AuthEnrollmentProgress progress,
                       std::optional<brillo::Blob> nonce) {
    enroll_callback_.Run(progress, nonce);
  }

  MockBiometricsCommandProcessor* mock_processor_;
  base::RepeatingCallback<void(user_data_auth::AuthEnrollmentProgress,
                               std::optional<brillo::Blob>)>
      enroll_callback_;
  RepeatingTestFuture<user_data_auth::AuthEnrollmentProgress> enroll_signals_;
  std::unique_ptr<BiometricsAuthBlockService> service_;
};

TEST_F(BiometricsAuthBlockServiceTest, StartEnrollSuccess) {
  base::OnceCallback<void(bool)> start_session_callback;
  EXPECT_CALL(*mock_processor_, StartEnrollSession(_))
      .WillOnce(SaveStartEnrollSessionCallback{&start_session_callback});

  TestFuture<CryptohomeStatusOr<std::unique_ptr<PreparedAuthFactorToken>>>
      start_result;
  service_->StartEnrollSession(AuthFactorType::kFingerprint, kFakeUserId,
                               start_result.GetCallback());

  ASSERT_FALSE(start_result.IsReady());
  std::move(start_session_callback).Run(true);
  ASSERT_TRUE(start_result.IsReady());
  EXPECT_THAT(start_result.Get(), IsOk());

  EXPECT_CALL(*mock_processor_, EndEnrollSession);
}

TEST_F(BiometricsAuthBlockServiceTest, StartEnrollAgainFailure) {
  EXPECT_CALL(*mock_processor_, StartEnrollSession(_))
      .WillOnce([&](auto&& callback) { std::move(callback).Run(true); });

  // Kick off the 1st start.
  TestFuture<CryptohomeStatusOr<std::unique_ptr<PreparedAuthFactorToken>>>
      start_result;
  service_->StartEnrollSession(AuthFactorType::kFingerprint, kFakeUserId,
                               start_result.GetCallback());
  ASSERT_TRUE(start_result.IsReady());
  EXPECT_THAT(start_result.Get(), IsOk());

  // Kick off the 2nd start.
  TestFuture<CryptohomeStatusOr<std::unique_ptr<PreparedAuthFactorToken>>>
      second_start_result;
  service_->StartEnrollSession(AuthFactorType::kFingerprint, kFakeUserId,
                               second_start_result.GetCallback());
  ASSERT_TRUE(second_start_result.IsReady());
  EXPECT_EQ(second_start_result.Get().status()->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_BIOMETRICS_BUSY);

  EXPECT_CALL(*mock_processor_, EndEnrollSession);
}

TEST_F(BiometricsAuthBlockServiceTest, StartEnrollDuringPendingSessionFailure) {
  EXPECT_CALL(*mock_processor_, StartEnrollSession(_)).Times(1);

  // Kick off the 1st start.
  TestFuture<CryptohomeStatusOr<std::unique_ptr<PreparedAuthFactorToken>>>
      start_result;
  service_->StartEnrollSession(AuthFactorType::kFingerprint, kFakeUserId,
                               start_result.GetCallback());
  ASSERT_FALSE(start_result.IsReady());

  // Kick off the 2nd start.
  TestFuture<CryptohomeStatusOr<std::unique_ptr<PreparedAuthFactorToken>>>
      second_start_result;
  service_->StartEnrollSession(AuthFactorType::kFingerprint, kFakeUserId,
                               second_start_result.GetCallback());
  ASSERT_TRUE(second_start_result.IsReady());
  EXPECT_EQ(second_start_result.Get().status()->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_BIOMETRICS_BUSY);
}

// Starting a second enroll session after the first one failed should be
// successful.
TEST_F(BiometricsAuthBlockServiceTest, StartEnrollAgainSuccess) {
  base::OnceCallback<void(bool)> start_session_callback1,
      start_session_callback2;

  {
    InSequence s;
    EXPECT_CALL(*mock_processor_, StartEnrollSession(_))
        .WillOnce(SaveStartEnrollSessionCallback{&start_session_callback1});
    EXPECT_CALL(*mock_processor_, StartEnrollSession(_))
        .WillOnce(SaveStartEnrollSessionCallback{&start_session_callback2});
  }

  // Kick off the 1st start.
  TestFuture<CryptohomeStatusOr<std::unique_ptr<PreparedAuthFactorToken>>>
      start_result;
  service_->StartEnrollSession(AuthFactorType::kFingerprint, kFakeUserId,
                               start_result.GetCallback());
  ASSERT_FALSE(start_result.IsReady());
  std::move(start_session_callback1).Run(false);
  ASSERT_TRUE(start_result.IsReady());
  EXPECT_THAT(start_result.Get().status()->local_legacy_error(),
              user_data_auth::CRYPTOHOME_ERROR_FINGERPRINT_ERROR_INTERNAL);

  // Kick off the 2nd start.
  TestFuture<CryptohomeStatusOr<std::unique_ptr<PreparedAuthFactorToken>>>
      second_start_result;
  service_->StartEnrollSession(AuthFactorType::kFingerprint, kFakeUserId,
                               second_start_result.GetCallback());
  ASSERT_FALSE(second_start_result.IsReady());
  std::move(start_session_callback2).Run(true);
  ASSERT_TRUE(start_result.IsReady());
  EXPECT_THAT(second_start_result.Get(), IsOk());

  EXPECT_CALL(*mock_processor_, EndEnrollSession).Times(1);
}

TEST_F(BiometricsAuthBlockServiceTest, ReceiveEnrollSignalSuccess) {
  const brillo::Blob kFakeNonce(32, 1);

  base::OnceCallback<void(bool)> start_session_callback;
  EXPECT_CALL(*mock_processor_, StartEnrollSession(_))
      .WillOnce(SaveStartEnrollSessionCallback{&start_session_callback});

  TestFuture<CryptohomeStatusOr<std::unique_ptr<PreparedAuthFactorToken>>>
      start_result;
  service_->StartEnrollSession(AuthFactorType::kFingerprint, kFakeUserId,
                               start_result.GetCallback());

  ASSERT_FALSE(start_result.IsReady());
  std::move(start_session_callback).Run(true);
  ASSERT_TRUE(start_result.IsReady());
  EXPECT_THAT(start_result.Get(), IsOk());

  AuthEnrollmentProgress event1 = ConstructAuthEnrollmentProgress(
      FingerprintScanResult::FINGERPRINT_SCAN_RESULT_SUCCESS, 50);
  EmitEnrollEvent(event1, std::nullopt);
  ASSERT_FALSE(enroll_signals_.IsEmpty());
  EXPECT_THAT(enroll_signals_.Take(), ProtoEq(event1));
  EXPECT_EQ(service_->TakeNonce(), std::nullopt);

  AuthEnrollmentProgress event2 = ConstructAuthEnrollmentProgress(
      FingerprintScanResult::FINGERPRINT_SCAN_RESULT_SUCCESS, 100);
  EmitEnrollEvent(event2, kFakeNonce);
  ASSERT_FALSE(enroll_signals_.IsEmpty());
  EXPECT_THAT(enroll_signals_.Take(), ProtoEq(event2));
  EXPECT_EQ(service_->TakeNonce(), kFakeNonce);

  ASSERT_TRUE(enroll_signals_.IsEmpty());
  EXPECT_EQ(service_->TakeNonce(), std::nullopt);

  EXPECT_CALL(*mock_processor_, EndEnrollSession);
}

TEST_F(BiometricsAuthBlockServiceTest, CreateCredentialSuccess) {
  const BiometricsCommandProcessor::OperationInput kFakeInput{
      .nonce = brillo::Blob(32, 1),
      .encrypted_label_seed = brillo::Blob(32, 2),
      .iv = brillo::Blob(16, 3),
  };
  const BiometricsCommandProcessor::OperationOutput kFakeOutput{
      .record_id = "fake_id",
      .auth_secret = brillo::SecureBlob(32, 1),
      .auth_pin = brillo::SecureBlob(32, 2),
  };

  base::OnceCallback<void(bool)> start_session_callback;
  EXPECT_CALL(*mock_processor_, StartEnrollSession(_))
      .WillOnce(SaveStartEnrollSessionCallback{&start_session_callback});

  TestFuture<CryptohomeStatusOr<std::unique_ptr<PreparedAuthFactorToken>>>
      start_result;
  service_->StartEnrollSession(AuthFactorType::kFingerprint, kFakeUserId,
                               start_result.GetCallback());

  ASSERT_FALSE(start_result.IsReady());
  std::move(start_session_callback).Run(true);
  ASSERT_TRUE(start_result.IsReady());
  EXPECT_THAT(start_result.Get(), IsOk());

  BiometricsCommandProcessor::OperationCallback create_credential_callback;
  EXPECT_CALL(*mock_processor_,
              CreateCredential(kFakeUserId, OperationInputEq(kFakeInput), _))
      .WillOnce(SaveCreateCredentialCallback{&create_credential_callback});

  TestFuture<CryptohomeStatusOr<BiometricsCommandProcessor::OperationOutput>>
      create_credential_result;
  service_->CreateCredential(kFakeInput,
                             create_credential_result.GetCallback());

  ASSERT_FALSE(create_credential_result.IsReady());
  std::move(create_credential_callback).Run(kFakeOutput);
  ASSERT_TRUE(create_credential_result.IsReady());
  EXPECT_THAT(create_credential_result.Get(),
              IsOkAnd(OperationOutputEq(kFakeOutput)));

  EXPECT_CALL(*mock_processor_, EndEnrollSession);
}

TEST_F(BiometricsAuthBlockServiceTest, CreateCredentialNoSessionFailure) {
  const BiometricsCommandProcessor::OperationInput kFakeInput{
      .nonce = brillo::Blob(32, 1),
      .encrypted_label_seed = brillo::Blob(32, 2),
      .iv = brillo::Blob(16, 3),
  };

  EXPECT_CALL(*mock_processor_, CreateCredential).Times(0);

  RepeatingTestFuture<
      CryptohomeStatusOr<BiometricsCommandProcessor::OperationOutput>>
      create_credential_result;
  service_->CreateCredential(kFakeInput,
                             create_credential_result.GetCallback());

  ASSERT_FALSE(create_credential_result.IsEmpty());
  EXPECT_EQ(create_credential_result.Take().status()->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_FINGERPRINT_ERROR_INTERNAL);

  // After a session is terminated, CreateCredential should fail too.

  EXPECT_CALL(*mock_processor_, StartEnrollSession(_))
      .WillOnce([&](auto&& callback) { std::move(callback).Run(true); });

  TestFuture<CryptohomeStatusOr<std::unique_ptr<PreparedAuthFactorToken>>>
      start_result;
  service_->StartEnrollSession(AuthFactorType::kFingerprint, kFakeUserId,
                               start_result.GetCallback());
  ASSERT_TRUE(start_result.IsReady());
  EXPECT_THAT(start_result.Get(), IsOk());

  // Destruction of the token shouldn't result in a call to mock_processor_
  // again.
  EXPECT_CALL(*mock_processor_, EndEnrollSession).Times(1);
  service_->EndEnrollSession();

  // Test that CreateCredential fails after a terminated session.
  service_->CreateCredential(kFakeInput,
                             create_credential_result.GetCallback());

  ASSERT_FALSE(create_credential_result.IsEmpty());
  EXPECT_EQ(create_credential_result.Take().status()->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_FINGERPRINT_ERROR_INTERNAL);
}

}  // namespace
}  // namespace cryptohome
