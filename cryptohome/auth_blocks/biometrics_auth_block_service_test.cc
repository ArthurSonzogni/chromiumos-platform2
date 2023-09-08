// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_blocks/biometrics_auth_block_service.h"

#include <utility>

#include <base/functional/callback.h>
#include <base/test/repeating_test_future.h>
#include <base/task/sequenced_task_runner.h>
#include <base/test/task_environment.h>
#include <base/test/test_future.h>
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

using DeleteResult = BiometricsAuthBlockService::DeleteResult;

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

// Compares OperationOutput structs.
MATCHER_P(OperationOutputEq, output, "") {
  return arg.record_id == output.record_id &&
         arg.auth_secret == output.auth_secret &&
         arg.auth_pin == output.auth_pin;
}

// Functors for saving on_done callbacks of biometrics command processor methods
// into a given argument. Useful for mocking out the biometrics command
// processor methods.
struct SaveStartEnrollSessionCallback {
  void operator()(BiometricsCommandProcessor::OperationInput input,
                  base::OnceCallback<void(bool)> callback) {
    *captured_callback = std::move(callback);
  }
  base::OnceCallback<void(bool)>* captured_callback;
};

struct SaveCreateCredentialCallback {
  void operator()(

      BiometricsCommandProcessor::OperationCallback callback) {
    *captured_callback = std::move(callback);
  }
  BiometricsCommandProcessor::OperationCallback* captured_callback;
};

struct SaveStartAuthenticateSessionCallback {
  void operator()(ObfuscatedUsername,
                  BiometricsCommandProcessor::OperationInput,
                  base::OnceCallback<void(bool)> callback) {
    *captured_callback = std::move(callback);
  }
  base::OnceCallback<void(bool)>* captured_callback;
};

struct SaveMatchCredentialCallback {
  void operator()(BiometricsCommandProcessor::OperationCallback callback) {
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

BiometricsCommandProcessor::OperationInput GetFakeInput() {
  return {
      .nonce = brillo::Blob(32, 1),
      .encrypted_label_seed = brillo::Blob(32, 2),
      .iv = brillo::Blob(16, 3),
  };
}

BiometricsCommandProcessor::OperationOutput GetFakeOutput() {
  return {
      .record_id = "fake_id",
      .auth_secret = brillo::SecureBlob(32, 1),
      .auth_pin = brillo::SecureBlob(32, 2),
  };
}

// Base test fixture which sets up the task environment.
class BaseTestFixture : public ::testing::Test {
 protected:
  base::test::SingleThreadTaskEnvironment task_environment_ = {
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  scoped_refptr<base::SequencedTaskRunner> task_runner_ =
      base::SequencedTaskRunner::GetCurrentDefault();
};

class BiometricsAuthBlockServiceTest : public BaseTestFixture {
 public:
  void SetUp() override {
    auto mock_processor = std::make_unique<MockBiometricsCommandProcessor>();
    mock_processor_ = mock_processor.get();
    EXPECT_CALL(*mock_processor_, SetEnrollScanDoneCallback(_))
        .WillOnce(SaveArg<0>(&enroll_callback_));
    EXPECT_CALL(*mock_processor_, SetAuthScanDoneCallback(_))
        .WillOnce(SaveArg<0>(&auth_callback_));
    EXPECT_CALL(*mock_processor_, SetSessionFailedCallback(_))
        .WillOnce(SaveArg<0>(&session_failed_callback_));
    service_ = std::make_unique<BiometricsAuthBlockService>(
        std::move(mock_processor), enroll_signals_.GetCallback(),
        auth_signals_.GetCallback());
  }

 protected:
  const ObfuscatedUsername kFakeUserId{"fake"};

  void EmitEnrollEvent(user_data_auth::AuthEnrollmentProgress progress) {
    enroll_callback_.Run(progress);
  }

  void EmitAuthEvent(user_data_auth::AuthScanDone auth_scan) {
    auth_callback_.Run(auth_scan);
  }

  void EmitSessionFailedEvent() { session_failed_callback_.Run(); }

  MockBiometricsCommandProcessor* mock_processor_;
  base::RepeatingCallback<void(user_data_auth::AuthEnrollmentProgress)>
      enroll_callback_;
  RepeatingTestFuture<user_data_auth::AuthEnrollmentProgress> enroll_signals_;
  base::RepeatingCallback<void(user_data_auth::AuthScanDone)> auth_callback_;
  RepeatingTestFuture<user_data_auth::AuthScanDone> auth_signals_;
  base::RepeatingCallback<void()> session_failed_callback_;
  std::unique_ptr<BiometricsAuthBlockService> service_;
};

TEST_F(BiometricsAuthBlockServiceTest, StartEnrollSuccess) {
  base::OnceCallback<void(bool)> start_session_callback;
  EXPECT_CALL(*mock_processor_, StartEnrollSession(_, _))
      .WillOnce(SaveStartEnrollSessionCallback{&start_session_callback});

  TestFuture<CryptohomeStatusOr<std::unique_ptr<PreparedAuthFactorToken>>>
      start_result;
  service_->StartEnrollSession(AuthFactorType::kFingerprint, GetFakeInput(),
                               start_result.GetCallback());

  ASSERT_FALSE(start_result.IsReady());
  std::move(start_session_callback).Run(true);
  ASSERT_TRUE(start_result.IsReady());
  EXPECT_THAT(start_result.Get(), IsOk());

  EXPECT_CALL(*mock_processor_, EndEnrollSession);
}

TEST_F(BiometricsAuthBlockServiceTest, StartEnrollAgainFailure) {
  EXPECT_CALL(*mock_processor_, StartEnrollSession(_, _))
      .WillOnce(
          [&](auto&&, auto&& callback) { std::move(callback).Run(true); });

  // Kick off the 1st start.
  TestFuture<CryptohomeStatusOr<std::unique_ptr<PreparedAuthFactorToken>>>
      start_result;
  service_->StartEnrollSession(AuthFactorType::kFingerprint, GetFakeInput(),
                               start_result.GetCallback());
  ASSERT_TRUE(start_result.IsReady());
  EXPECT_THAT(start_result.Get(), IsOk());

  // Kick off the 2nd start.
  TestFuture<CryptohomeStatusOr<std::unique_ptr<PreparedAuthFactorToken>>>
      second_start_result;
  service_->StartEnrollSession(AuthFactorType::kFingerprint, GetFakeInput(),
                               second_start_result.GetCallback());
  ASSERT_TRUE(second_start_result.IsReady());
  EXPECT_EQ(second_start_result.Get().status()->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_BIOMETRICS_BUSY);

  EXPECT_CALL(*mock_processor_, EndEnrollSession);
}

TEST_F(BiometricsAuthBlockServiceTest, StartEnrollDuringPendingSessionFailure) {
  EXPECT_CALL(*mock_processor_, StartEnrollSession(_, _)).Times(1);

  // Kick off the 1st start.
  TestFuture<CryptohomeStatusOr<std::unique_ptr<PreparedAuthFactorToken>>>
      start_result;
  service_->StartEnrollSession(AuthFactorType::kFingerprint, GetFakeInput(),
                               start_result.GetCallback());
  ASSERT_FALSE(start_result.IsReady());

  // Kick off the 2nd start.
  TestFuture<CryptohomeStatusOr<std::unique_ptr<PreparedAuthFactorToken>>>
      second_start_result;
  service_->StartEnrollSession(AuthFactorType::kFingerprint, GetFakeInput(),
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
    EXPECT_CALL(*mock_processor_, StartEnrollSession(_, _))
        .WillOnce(SaveStartEnrollSessionCallback{&start_session_callback1});
    EXPECT_CALL(*mock_processor_, StartEnrollSession(_, _))
        .WillOnce(SaveStartEnrollSessionCallback{&start_session_callback2});
  }

  // Kick off the 1st start.
  TestFuture<CryptohomeStatusOr<std::unique_ptr<PreparedAuthFactorToken>>>
      start_result;
  service_->StartEnrollSession(AuthFactorType::kFingerprint, GetFakeInput(),
                               start_result.GetCallback());
  ASSERT_FALSE(start_result.IsReady());
  std::move(start_session_callback1).Run(false);
  ASSERT_TRUE(start_result.IsReady());
  EXPECT_THAT(start_result.Get().status()->local_legacy_error(),
              user_data_auth::CRYPTOHOME_ERROR_FINGERPRINT_ERROR_INTERNAL);

  // Kick off the 2nd start.
  TestFuture<CryptohomeStatusOr<std::unique_ptr<PreparedAuthFactorToken>>>
      second_start_result;
  service_->StartEnrollSession(AuthFactorType::kFingerprint, GetFakeInput(),
                               second_start_result.GetCallback());
  ASSERT_FALSE(second_start_result.IsReady());
  std::move(start_session_callback2).Run(true);
  ASSERT_TRUE(start_result.IsReady());
  EXPECT_THAT(second_start_result.Get(), IsOk());

  EXPECT_CALL(*mock_processor_, EndEnrollSession).Times(1);
}

TEST_F(BiometricsAuthBlockServiceTest, ReceiveEnrollSignalSuccess) {
  base::OnceCallback<void(bool)> start_session_callback;
  EXPECT_CALL(*mock_processor_, StartEnrollSession(_, _))
      .WillOnce(SaveStartEnrollSessionCallback{&start_session_callback});

  TestFuture<CryptohomeStatusOr<std::unique_ptr<PreparedAuthFactorToken>>>
      start_result;
  service_->StartEnrollSession(AuthFactorType::kFingerprint, GetFakeInput(),
                               start_result.GetCallback());

  ASSERT_FALSE(start_result.IsReady());
  std::move(start_session_callback).Run(true);
  ASSERT_TRUE(start_result.IsReady());
  EXPECT_THAT(start_result.Get(), IsOk());

  AuthEnrollmentProgress event1 = ConstructAuthEnrollmentProgress(
      FingerprintScanResult::FINGERPRINT_SCAN_RESULT_SUCCESS, 50);
  EmitEnrollEvent(event1);
  ASSERT_FALSE(enroll_signals_.IsEmpty());
  EXPECT_THAT(enroll_signals_.Take(), ProtoEq(event1));

  AuthEnrollmentProgress event2 = ConstructAuthEnrollmentProgress(
      FingerprintScanResult::FINGERPRINT_SCAN_RESULT_SUCCESS, 100);
  EmitEnrollEvent(event2);
  ASSERT_FALSE(enroll_signals_.IsEmpty());
  EXPECT_THAT(enroll_signals_.Take(), ProtoEq(event2));

  ASSERT_TRUE(enroll_signals_.IsEmpty());

  EXPECT_CALL(*mock_processor_, EndEnrollSession);
}

TEST_F(BiometricsAuthBlockServiceTest, ReceiveEnrollSignalPendingSessionStart) {
  base::OnceCallback<void(bool)> start_session_callback;
  EXPECT_CALL(*mock_processor_, StartEnrollSession(_, _))
      .WillOnce(SaveStartEnrollSessionCallback{&start_session_callback});

  TestFuture<CryptohomeStatusOr<std::unique_ptr<PreparedAuthFactorToken>>>
      start_result;
  service_->StartEnrollSession(AuthFactorType::kFingerprint, GetFakeInput(),
                               start_result.GetCallback());

  ASSERT_FALSE(start_result.IsReady());

  AuthEnrollmentProgress event1 = ConstructAuthEnrollmentProgress(
      FingerprintScanResult::FINGERPRINT_SCAN_RESULT_SUCCESS, 50);
  EmitEnrollEvent(event1);
  ASSERT_FALSE(enroll_signals_.IsEmpty());
  EXPECT_THAT(enroll_signals_.Take(), ProtoEq(event1));

  AuthEnrollmentProgress event2 = ConstructAuthEnrollmentProgress(
      FingerprintScanResult::FINGERPRINT_SCAN_RESULT_SUCCESS, 100);
  EmitEnrollEvent(event2);
  ASSERT_FALSE(enroll_signals_.IsEmpty());
  EXPECT_THAT(enroll_signals_.Take(), ProtoEq(event2));

  ASSERT_TRUE(enroll_signals_.IsEmpty());

  std::move(start_session_callback).Run(true);
  ASSERT_TRUE(start_result.IsReady());
  EXPECT_THAT(start_result.Get(), IsOk());
  EXPECT_CALL(*mock_processor_, EndEnrollSession);
}

TEST_F(BiometricsAuthBlockServiceTest, ReceiveEmptyEnrollSignalWithoutSession) {
  AuthEnrollmentProgress event = ConstructAuthEnrollmentProgress(
      FingerprintScanResult::FINGERPRINT_SCAN_RESULT_SUCCESS, 100);
  EmitEnrollEvent(event);
  ASSERT_TRUE(enroll_signals_.IsEmpty());
}

TEST_F(BiometricsAuthBlockServiceTest, SessionFailedInEnrollSession) {
  EXPECT_CALL(*mock_processor_, StartEnrollSession(_, _))
      .Times(2)
      .WillRepeatedly(
          [&](auto&&, auto&& callback) { std::move(callback).Run(true); });

  TestFuture<CryptohomeStatusOr<std::unique_ptr<PreparedAuthFactorToken>>>
      start_result;
  service_->StartEnrollSession(AuthFactorType::kFingerprint, GetFakeInput(),
                               start_result.GetCallback());

  ASSERT_TRUE(start_result.IsReady());
  EXPECT_THAT(start_result.Get(), IsOk());

  EmitSessionFailedEvent();
  user_data_auth::AuthEnrollmentProgress event;
  event.mutable_scan_result()->set_fingerprint_result(
      FingerprintScanResult::FINGERPRINT_SCAN_RESULT_FATAL_ERROR);
  EXPECT_FALSE(enroll_signals_.IsEmpty());
  EXPECT_THAT(enroll_signals_.Take(), ProtoEq(event));

  // Test that we can start a new session now because the previous session has
  // ended.
  TestFuture<CryptohomeStatusOr<std::unique_ptr<PreparedAuthFactorToken>>>
      second_start_result;
  service_->StartEnrollSession(AuthFactorType::kFingerprint, GetFakeInput(),
                               second_start_result.GetCallback());

  ASSERT_TRUE(second_start_result.IsReady());
  EXPECT_THAT(second_start_result.Get(), IsOk());

  EXPECT_CALL(*mock_processor_, EndEnrollSession);
}

TEST_F(BiometricsAuthBlockServiceTest, CreateCredentialSuccess) {
  base::OnceCallback<void(bool)> start_session_callback;
  EXPECT_CALL(*mock_processor_, StartEnrollSession(_, _))
      .WillOnce(SaveStartEnrollSessionCallback{&start_session_callback});

  TestFuture<CryptohomeStatusOr<std::unique_ptr<PreparedAuthFactorToken>>>
      start_result;
  service_->StartEnrollSession(AuthFactorType::kFingerprint, GetFakeInput(),
                               start_result.GetCallback());

  ASSERT_FALSE(start_result.IsReady());
  std::move(start_session_callback).Run(true);
  ASSERT_TRUE(start_result.IsReady());
  EXPECT_THAT(start_result.Get(), IsOk());

  BiometricsCommandProcessor::OperationCallback create_credential_callback;
  EXPECT_CALL(*mock_processor_, CreateCredential(_))
      .WillOnce(SaveCreateCredentialCallback{&create_credential_callback});

  TestFuture<CryptohomeStatusOr<BiometricsCommandProcessor::OperationOutput>>
      create_credential_result;
  service_->CreateCredential(create_credential_result.GetCallback());

  ASSERT_FALSE(create_credential_result.IsReady());
  std::move(create_credential_callback).Run(GetFakeOutput());
  ASSERT_TRUE(create_credential_result.IsReady());
  EXPECT_THAT(create_credential_result.Get(),
              IsOkAnd(OperationOutputEq(GetFakeOutput())));

  EXPECT_CALL(*mock_processor_, EndEnrollSession);
}

TEST_F(BiometricsAuthBlockServiceTest, CreateCredentialNoSessionFailure) {
  EXPECT_CALL(*mock_processor_, CreateCredential).Times(0);

  RepeatingTestFuture<
      CryptohomeStatusOr<BiometricsCommandProcessor::OperationOutput>>
      create_credential_result;
  service_->CreateCredential(create_credential_result.GetCallback());

  ASSERT_FALSE(create_credential_result.IsEmpty());
  EXPECT_EQ(create_credential_result.Take().status()->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_FINGERPRINT_ERROR_INTERNAL);

  // After a session is terminated, CreateCredential should fail too.

  EXPECT_CALL(*mock_processor_, StartEnrollSession(_, _))
      .WillOnce(
          [&](auto&&, auto&& callback) { std::move(callback).Run(true); });

  TestFuture<CryptohomeStatusOr<std::unique_ptr<PreparedAuthFactorToken>>>
      start_result;
  service_->StartEnrollSession(AuthFactorType::kFingerprint, GetFakeInput(),
                               start_result.GetCallback());
  ASSERT_TRUE(start_result.IsReady());
  EXPECT_THAT(start_result.Get(), IsOk());

  // Destruction of the token shouldn't result in a call to mock_processor_
  // again.
  EXPECT_CALL(*mock_processor_, EndEnrollSession).Times(1);
  service_->EndEnrollSession();

  // Test that CreateCredential fails after a terminated session.
  service_->CreateCredential(create_credential_result.GetCallback());

  ASSERT_FALSE(create_credential_result.IsEmpty());
  EXPECT_EQ(create_credential_result.Take().status()->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_FINGERPRINT_ERROR_INTERNAL);
}

TEST_F(BiometricsAuthBlockServiceTest, StartAuthenticateSuccess) {
  base::OnceCallback<void(bool)> start_session_callback;
  EXPECT_CALL(*mock_processor_, StartAuthenticateSession(kFakeUserId, _, _))
      .WillOnce(SaveStartAuthenticateSessionCallback{&start_session_callback});

  TestFuture<CryptohomeStatusOr<std::unique_ptr<PreparedAuthFactorToken>>>
      start_result;
  service_->StartAuthenticateSession(AuthFactorType::kFingerprint, kFakeUserId,
                                     GetFakeInput(),
                                     start_result.GetCallback());

  ASSERT_FALSE(start_result.IsReady());
  std::move(start_session_callback).Run(true);
  ASSERT_TRUE(start_result.IsReady());
  EXPECT_THAT(start_result.Get(), IsOk());

  EXPECT_CALL(*mock_processor_, EndAuthenticateSession);
}

TEST_F(BiometricsAuthBlockServiceTest,
       StartAuthenticateDuringPendingSessionFailure) {
  EXPECT_CALL(*mock_processor_, StartAuthenticateSession(kFakeUserId, _, _))
      .Times(1);

  // Kick off the 1st start.
  TestFuture<CryptohomeStatusOr<std::unique_ptr<PreparedAuthFactorToken>>>
      start_result;
  service_->StartAuthenticateSession(AuthFactorType::kFingerprint, kFakeUserId,
                                     GetFakeInput(),
                                     start_result.GetCallback());
  ASSERT_FALSE(start_result.IsReady());

  // Kick off the 2nd start.
  TestFuture<CryptohomeStatusOr<std::unique_ptr<PreparedAuthFactorToken>>>
      second_start_result;
  service_->StartAuthenticateSession(AuthFactorType::kFingerprint, kFakeUserId,
                                     GetFakeInput(),
                                     second_start_result.GetCallback());
  ASSERT_TRUE(second_start_result.IsReady());
  EXPECT_EQ(second_start_result.Get().status()->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_BIOMETRICS_BUSY);
}

// Starting a second enroll session after the first one failed should be
// successful.
TEST_F(BiometricsAuthBlockServiceTest, StartAuthenticateAgainSuccess) {
  base::OnceCallback<void(bool)> start_session_callback1,
      start_session_callback2;

  {
    InSequence s;
    EXPECT_CALL(*mock_processor_, StartAuthenticateSession(kFakeUserId, _, _))
        .WillOnce(
            SaveStartAuthenticateSessionCallback{&start_session_callback1});
    EXPECT_CALL(*mock_processor_, StartAuthenticateSession(kFakeUserId, _, _))
        .WillOnce(
            SaveStartAuthenticateSessionCallback{&start_session_callback2});
  }

  // Kick off the 1st start.
  TestFuture<CryptohomeStatusOr<std::unique_ptr<PreparedAuthFactorToken>>>
      start_result;
  service_->StartAuthenticateSession(AuthFactorType::kFingerprint, kFakeUserId,
                                     GetFakeInput(),
                                     start_result.GetCallback());
  ASSERT_FALSE(start_result.IsReady());
  std::move(start_session_callback1).Run(false);
  ASSERT_TRUE(start_result.IsReady());
  EXPECT_THAT(start_result.Get().status()->local_legacy_error(),
              user_data_auth::CRYPTOHOME_ERROR_FINGERPRINT_ERROR_INTERNAL);

  // Kick off the 2nd start.
  TestFuture<CryptohomeStatusOr<std::unique_ptr<PreparedAuthFactorToken>>>
      second_start_result;
  service_->StartAuthenticateSession(AuthFactorType::kFingerprint, kFakeUserId,
                                     GetFakeInput(),
                                     second_start_result.GetCallback());
  ASSERT_FALSE(second_start_result.IsReady());
  std::move(start_session_callback2).Run(true);
  ASSERT_TRUE(start_result.IsReady());
  EXPECT_THAT(second_start_result.Get(), IsOk());

  EXPECT_CALL(*mock_processor_, EndAuthenticateSession).Times(1);
}

TEST_F(BiometricsAuthBlockServiceTest, ReceiveAuthenticateSignalSuccess) {
  base::OnceCallback<void(bool)> start_session_callback;
  EXPECT_CALL(*mock_processor_, StartAuthenticateSession(kFakeUserId, _, _))
      .WillOnce(SaveStartAuthenticateSessionCallback{&start_session_callback});

  TestFuture<CryptohomeStatusOr<std::unique_ptr<PreparedAuthFactorToken>>>
      start_result;
  service_->StartAuthenticateSession(AuthFactorType::kFingerprint, kFakeUserId,
                                     GetFakeInput(),
                                     start_result.GetCallback());

  ASSERT_FALSE(start_result.IsReady());
  std::move(start_session_callback).Run(true);
  ASSERT_TRUE(start_result.IsReady());
  EXPECT_THAT(start_result.Get(), IsOk());

  user_data_auth::AuthScanDone event;
  event.mutable_scan_result()->set_fingerprint_result(
      FingerprintScanResult::FINGERPRINT_SCAN_RESULT_SUCCESS);
  EmitAuthEvent(event);
  ASSERT_FALSE(auth_signals_.IsEmpty());
  EXPECT_THAT(auth_signals_.Take(), ProtoEq(event));

  ASSERT_TRUE(auth_signals_.IsEmpty());

  EXPECT_CALL(*mock_processor_, EndAuthenticateSession);
}

TEST_F(BiometricsAuthBlockServiceTest,
       ReceiveAuthenticateSignalPendingSessionStart) {
  base::OnceCallback<void(bool)> start_session_callback;
  EXPECT_CALL(*mock_processor_, StartAuthenticateSession(kFakeUserId, _, _))
      .WillOnce(SaveStartAuthenticateSessionCallback{&start_session_callback});

  TestFuture<CryptohomeStatusOr<std::unique_ptr<PreparedAuthFactorToken>>>
      start_result;
  service_->StartAuthenticateSession(AuthFactorType::kFingerprint, kFakeUserId,
                                     GetFakeInput(),
                                     start_result.GetCallback());

  ASSERT_FALSE(start_result.IsReady());

  user_data_auth::AuthScanDone event;
  event.mutable_scan_result()->set_fingerprint_result(
      FingerprintScanResult::FINGERPRINT_SCAN_RESULT_SUCCESS);
  EmitAuthEvent(event);
  ASSERT_FALSE(auth_signals_.IsEmpty());
  EXPECT_THAT(auth_signals_.Take(), ProtoEq(event));

  ASSERT_TRUE(auth_signals_.IsEmpty());

  std::move(start_session_callback).Run(true);
  ASSERT_TRUE(start_result.IsReady());
  EXPECT_THAT(start_result.Get(), IsOk());
  EXPECT_CALL(*mock_processor_, EndAuthenticateSession);
}

TEST_F(BiometricsAuthBlockServiceTest,
       ReceiveEmptyAuthenticateSignalWithoutSession) {
  user_data_auth::AuthScanDone event;
  event.mutable_scan_result()->set_fingerprint_result(
      FingerprintScanResult::FINGERPRINT_SCAN_RESULT_SUCCESS);
  EmitAuthEvent(event);
  ASSERT_TRUE(auth_signals_.IsEmpty());
}

TEST_F(BiometricsAuthBlockServiceTest, SessionFailedInAuthenticateSession) {
  EXPECT_CALL(*mock_processor_, StartAuthenticateSession(kFakeUserId, _, _))
      .Times(2)
      .WillRepeatedly([&](auto&&, auto&&, auto&& callback) {
        std::move(callback).Run(true);
      });

  TestFuture<CryptohomeStatusOr<std::unique_ptr<PreparedAuthFactorToken>>>
      start_result;
  service_->StartAuthenticateSession(AuthFactorType::kFingerprint, kFakeUserId,
                                     GetFakeInput(),
                                     start_result.GetCallback());

  ASSERT_TRUE(start_result.IsReady());
  EXPECT_THAT(start_result.Get(), IsOk());

  EmitSessionFailedEvent();
  user_data_auth::AuthScanDone event;
  event.mutable_scan_result()->set_fingerprint_result(
      FingerprintScanResult::FINGERPRINT_SCAN_RESULT_FATAL_ERROR);
  EXPECT_FALSE(auth_signals_.IsEmpty());
  EXPECT_THAT(auth_signals_.Take(), ProtoEq(event));

  // Test that we can start a new session now because the previous session has
  // ended.
  TestFuture<CryptohomeStatusOr<std::unique_ptr<PreparedAuthFactorToken>>>
      second_start_result;
  service_->StartAuthenticateSession(AuthFactorType::kFingerprint, kFakeUserId,
                                     GetFakeInput(),
                                     second_start_result.GetCallback());

  ASSERT_TRUE(second_start_result.IsReady());
  EXPECT_THAT(second_start_result.Get(), IsOk());

  EXPECT_CALL(*mock_processor_, EndAuthenticateSession);
}

TEST_F(BiometricsAuthBlockServiceTest, MatchCredentialSuccess) {
  EXPECT_CALL(*mock_processor_, StartAuthenticateSession(kFakeUserId, _, _))
      .WillOnce([&](auto&&, auto&&, auto&& on_done) {
        std::move(on_done).Run(true);
      });
  EXPECT_CALL(*mock_processor_, EndAuthenticateSession);

  TestFuture<CryptohomeStatusOr<std::unique_ptr<PreparedAuthFactorToken>>>
      start_result;
  service_->StartAuthenticateSession(AuthFactorType::kFingerprint, kFakeUserId,
                                     GetFakeInput(),
                                     start_result.GetCallback());

  ASSERT_TRUE(start_result.IsReady());
  EXPECT_THAT(start_result.Get(), IsOk());

  BiometricsCommandProcessor::OperationCallback match_credential_callback;
  EXPECT_CALL(*mock_processor_, MatchCredential(_))
      .WillOnce(SaveMatchCredentialCallback{&match_credential_callback});

  TestFuture<CryptohomeStatusOr<BiometricsCommandProcessor::OperationOutput>>
      match_credential_result;
  service_->MatchCredential(match_credential_result.GetCallback());

  ASSERT_FALSE(match_credential_result.IsReady());
  std::move(match_credential_callback).Run(GetFakeOutput());
  ASSERT_TRUE(match_credential_result.IsReady());
  EXPECT_THAT(match_credential_result.Get(),
              IsOkAnd(OperationOutputEq(GetFakeOutput())));
  EXPECT_TRUE(auth_signals_.IsEmpty());
}

TEST_F(BiometricsAuthBlockServiceTest, MatchCredentialEndBeforeRestart) {
  EXPECT_CALL(*mock_processor_, StartAuthenticateSession(kFakeUserId, _, _))
      .WillOnce([&](auto&&, auto&&, auto&& on_done) {
        std::move(on_done).Run(true);
      });

  TestFuture<CryptohomeStatusOr<std::unique_ptr<PreparedAuthFactorToken>>>
      start_result;
  service_->StartAuthenticateSession(AuthFactorType::kFingerprint, kFakeUserId,
                                     GetFakeInput(),
                                     start_result.GetCallback());

  ASSERT_TRUE(start_result.IsReady());
  EXPECT_THAT(start_result.Get(), IsOk());

  BiometricsCommandProcessor::OperationCallback match_credential_callback;
  EXPECT_CALL(*mock_processor_, MatchCredential(_))
      .WillOnce(SaveMatchCredentialCallback{&match_credential_callback});

  TestFuture<CryptohomeStatusOr<BiometricsCommandProcessor::OperationOutput>>
      match_credential_result;
  service_->MatchCredential(match_credential_result.GetCallback());
  ASSERT_FALSE(match_credential_result.IsReady());

  EXPECT_CALL(*mock_processor_, EndAuthenticateSession).Times(1);
  // End the session before MatchCredential returns.
  service_->EndAuthenticateSession();

  // StartAuthenticateSession shouldn't be triggered again.
  std::move(match_credential_callback).Run(GetFakeOutput());
  ASSERT_TRUE(match_credential_result.IsReady());
  EXPECT_THAT(match_credential_result.Get(),
              IsOkAnd(OperationOutputEq(GetFakeOutput())));
  EXPECT_TRUE(auth_signals_.IsEmpty());
}

TEST_F(BiometricsAuthBlockServiceTest, MatchCredentialNoSessionFailure) {
  EXPECT_CALL(*mock_processor_, MatchCredential).Times(0);

  RepeatingTestFuture<
      CryptohomeStatusOr<BiometricsCommandProcessor::OperationOutput>>
      match_credential_result;
  service_->MatchCredential(match_credential_result.GetCallback());

  ASSERT_FALSE(match_credential_result.IsEmpty());
  EXPECT_EQ(match_credential_result.Take().status()->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_FINGERPRINT_ERROR_INTERNAL);

  // After a session is terminated, MatchCredential should fail too.

  EXPECT_CALL(*mock_processor_, StartAuthenticateSession(kFakeUserId, _, _))
      .WillOnce([&](auto&&, auto&&, auto&& callback) {
        std::move(callback).Run(true);
      });

  TestFuture<CryptohomeStatusOr<std::unique_ptr<PreparedAuthFactorToken>>>
      start_result;
  service_->StartAuthenticateSession(AuthFactorType::kFingerprint, kFakeUserId,
                                     GetFakeInput(),
                                     start_result.GetCallback());
  ASSERT_TRUE(start_result.IsReady());
  EXPECT_THAT(start_result.Get(), IsOk());

  // Destruction of the token shouldn't result in a call to mock_processor_
  // again.
  EXPECT_CALL(*mock_processor_, EndAuthenticateSession).Times(1);
  service_->EndAuthenticateSession();

  // Test that MatchCredential fails after a terminated session.
  service_->MatchCredential(match_credential_result.GetCallback());

  ASSERT_FALSE(match_credential_result.IsEmpty());
  EXPECT_EQ(match_credential_result.Take().status()->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_FINGERPRINT_ERROR_INTERNAL);
}

TEST_F(BiometricsAuthBlockServiceTest, EnrollSessionInvalidActions) {
  // Start an enroll session.
  EXPECT_CALL(*mock_processor_, StartEnrollSession(_, _))
      .WillOnce(
          [&](auto&&, auto&& callback) { std::move(callback).Run(true); });
  TestFuture<CryptohomeStatusOr<std::unique_ptr<PreparedAuthFactorToken>>>
      start_result;
  service_->StartEnrollSession(AuthFactorType::kFingerprint, GetFakeInput(),
                               start_result.GetCallback());
  ASSERT_TRUE(start_result.IsReady());
  EXPECT_THAT(start_result.Get(), IsOk());

  // Starting an authenticate session should fail.
  TestFuture<CryptohomeStatusOr<std::unique_ptr<PreparedAuthFactorToken>>>
      start_auth_result;
  service_->StartAuthenticateSession(AuthFactorType::kFingerprint, kFakeUserId,
                                     GetFakeInput(),
                                     start_auth_result.GetCallback());
  ASSERT_TRUE(start_auth_result.IsReady());
  EXPECT_EQ(start_auth_result.Get().status()->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_BIOMETRICS_BUSY);

  // MatchCredential should fail.
  EXPECT_CALL(*mock_processor_, MatchCredential).Times(0);
  TestFuture<CryptohomeStatusOr<BiometricsCommandProcessor::OperationOutput>>
      match_credential_result;
  service_->MatchCredential(match_credential_result.GetCallback());
  ASSERT_TRUE(match_credential_result.IsReady());
  EXPECT_EQ(match_credential_result.Take().status()->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_FINGERPRINT_ERROR_INTERNAL);

  // EndAuthenticateSession should do nothing.
  EXPECT_CALL(*mock_processor_, EndAuthenticateSession).Times(0);
  service_->EndAuthenticateSession();
}

TEST_F(BiometricsAuthBlockServiceTest, AuthenticateSessionInvalidActions) {
  // Start an authenticate session.
  EXPECT_CALL(*mock_processor_, StartAuthenticateSession(kFakeUserId, _, _))
      .WillOnce([&](auto&&, auto&&, auto&& callback) {
        std::move(callback).Run(true);
      });
  TestFuture<CryptohomeStatusOr<std::unique_ptr<PreparedAuthFactorToken>>>
      start_result;
  service_->StartAuthenticateSession(AuthFactorType::kFingerprint, kFakeUserId,
                                     GetFakeInput(),
                                     start_result.GetCallback());
  ASSERT_TRUE(start_result.IsReady());
  EXPECT_THAT(start_result.Get(), IsOk());

  // Starting an enroll session should fail.
  TestFuture<CryptohomeStatusOr<std::unique_ptr<PreparedAuthFactorToken>>>
      start_enroll_result;
  service_->StartEnrollSession(AuthFactorType::kFingerprint, GetFakeInput(),
                               start_enroll_result.GetCallback());
  ASSERT_TRUE(start_enroll_result.IsReady());
  EXPECT_EQ(start_enroll_result.Get().status()->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_BIOMETRICS_BUSY);

  // CreateCredential should fail.
  EXPECT_CALL(*mock_processor_, CreateCredential).Times(0);
  TestFuture<CryptohomeStatusOr<BiometricsCommandProcessor::OperationOutput>>
      create_credential_result;
  service_->CreateCredential(create_credential_result.GetCallback());
  ASSERT_TRUE(create_credential_result.IsReady());
  EXPECT_EQ(create_credential_result.Take().status()->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_FINGERPRINT_ERROR_INTERNAL);

  // EndEnrollSession should do nothing.
  EXPECT_CALL(*mock_processor_, EndEnrollSession).Times(0);
  service_->EndEnrollSession();
}

// Test that DeleteCredential simply proxies the call to command processor.
TEST_F(BiometricsAuthBlockServiceTest, DeleteCredential) {
  const std::string kRecordId("record_id");
  const DeleteResult kResult = DeleteResult::kSuccess;
  EXPECT_CALL(*mock_processor_, DeleteCredential(kFakeUserId, kRecordId, _))
      .WillOnce([kResult](auto&&, auto&&, auto&& callback) {
        std::move(callback).Run(kResult);
      });

  TestFuture<DeleteResult> delete_result;
  service_->DeleteCredential(kFakeUserId, kRecordId,
                             delete_result.GetCallback());
  EXPECT_EQ(delete_result.Get(), kResult);
}

}  // namespace
}  // namespace cryptohome
