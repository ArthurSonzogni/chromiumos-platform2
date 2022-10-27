// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_blocks/fp_service.h"

#include <string>
#include <utility>

#include <base/bind.h>
#include <base/test/repeating_test_future.h>
#include <base/task/sequenced_task_runner.h>
#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <base/threading/sequenced_task_runner_handle.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>
#include <gmock/gmock-spec-builders.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libhwsec-foundation/error/testing_helper.h>

#include "cryptohome/mock_fingerprint_manager.h"

namespace cryptohome {
namespace {

using base::test::RepeatingTestFuture;
using base::test::TestFuture;
using hwsec_foundation::error::testing::IsOk;

using ::testing::_;
using ::testing::Eq;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::StrictMock;

// Functor for saving off a fingerprint result callback into a given argument.
// Useful for mocking out SetSignalCallback to capture the parameter.
struct SaveSignalCallback {
  void operator()(FingerprintManager::SignalCallback callback) {
    *captured_callback = std::move(callback);
  }
  FingerprintManager::SignalCallback* captured_callback;
};

// Functor for saving off a fingerprint StartSession callback into a given
// argument. Useful for mocking out StartAuthSessionAsyncForUser to capture the
// parameter.
struct SaveStartSessionCallback {
  void operator()(std::string username,
                  FingerprintManager::StartSessionCallback callback) {
    *captured_callback = std::move(callback);
  }
  FingerprintManager::StartSessionCallback* captured_callback;
};

// Base test fixture which sets up the task environment.
class BaseTestFixture : public ::testing::Test {
 protected:
  base::test::SingleThreadTaskEnvironment task_environment_ = {
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  scoped_refptr<base::SequencedTaskRunner> task_runner_ =
      base::SequencedTaskRunnerHandle::Get();
};

// Test fixture for null service tests.
class NullFingerprintAuthBlockServiceTest : public BaseTestFixture {};

TEST_F(NullFingerprintAuthBlockServiceTest, NullVerifyFails) {
  auto service = FingerprintAuthBlockService::MakeNullService();

  TestFuture<CryptohomeStatus> on_done_result;
  service->Verify(on_done_result.GetCallback());

  ASSERT_THAT(on_done_result.IsReady(), IsTrue());
  EXPECT_THAT(on_done_result.Get()->local_legacy_error(),
              Eq(user_data_auth::CRYPTOHOME_ERROR_FINGERPRINT_ERROR_INTERNAL));
}

TEST_F(NullFingerprintAuthBlockServiceTest, NullStartFails) {
  auto service = FingerprintAuthBlockService::MakeNullService();
  std::string dummy_username = "dummy";

  TestFuture<CryptohomeStatus> on_done_result;
  service->Start(dummy_username, on_done_result.GetCallback());

  ASSERT_THAT(on_done_result.IsReady(), IsTrue());
  EXPECT_THAT(on_done_result.Get()->local_legacy_error(),
              Eq(user_data_auth::CRYPTOHOME_ERROR_ATTESTATION_NOT_READY));
}

// Test fixture for tests with a standard service instance constructed using a
// mock fingerprint service.
class FingerprintAuthBlockServiceTest : public BaseTestFixture {
 public:
  FingerprintAuthBlockServiceTest()
      : service_(base::BindRepeating(
                     &FingerprintAuthBlockServiceTest::GetFingerprintManager,
                     base::Unretained(this)),
                 base::BindRepeating(
                     &FingerprintAuthBlockServiceTest::OnFingerprintScanResult,
                     base::Unretained(this))) {}

  void TearDown() override {
    EXPECT_CALL(fp_manager_, EndAuthSession());
    service_.Terminate();
  }

 protected:
  FingerprintManager* GetFingerprintManager() { return &fp_manager_; }
  void OnFingerprintScanResult(user_data_auth::FingerprintScanResult result) {
    result_ = result;
  }

  StrictMock<MockFingerprintManager> fp_manager_;
  FingerprintAuthBlockService service_;
  std::string user_ = "dummy_user";
  user_data_auth::FingerprintScanResult result_;
};

TEST_F(FingerprintAuthBlockServiceTest, StartSuccess) {
  // Capture the callbacks from the fingerprint manager.
  EXPECT_CALL(fp_manager_, SetSignalCallback(_));
  FingerprintManager::StartSessionCallback start_session_callback;
  EXPECT_CALL(fp_manager_, StartAuthSessionAsyncForUser(_, _))
      .WillOnce(SaveStartSessionCallback{&start_session_callback});

  // Kick off the start.
  TestFuture<CryptohomeStatus> on_done_result;
  service_.Start(user_, on_done_result.GetCallback());

  // The on_done should only be triggered after we execute the callback from the
  // fingerprint manager.
  ASSERT_THAT(on_done_result.IsReady(), IsFalse());
  std::move(start_session_callback).Run(true);
  ASSERT_THAT(on_done_result.IsReady(), IsTrue());
  ASSERT_THAT(on_done_result.Get(), IsOk());
}

TEST_F(FingerprintAuthBlockServiceTest, StartAgainWithDifferentUserFailure) {
  EXPECT_CALL(fp_manager_, StartAuthSessionAsyncForUser(_, _));

  // Kick off the 1st start.
  TestFuture<CryptohomeStatus> on_done_result;
  service_.Start(user_, on_done_result.GetCallback());
  ASSERT_THAT(on_done_result.IsReady(), IsFalse());

  // Kick off the 2nd start.
  TestFuture<CryptohomeStatus> second_on_done_result;
  std::string another_user = "another_name";
  service_.Start(another_user, second_on_done_result.GetCallback());
  ASSERT_THAT(second_on_done_result.IsReady(), IsTrue());
  ASSERT_THAT(second_on_done_result.Get()->local_legacy_error(),
              Eq(user_data_auth::CRYPTOHOME_ERROR_FINGERPRINT_DENIED));
}

TEST_F(FingerprintAuthBlockServiceTest, StartAgainWithSameUserFailure) {
  EXPECT_CALL(fp_manager_, StartAuthSessionAsyncForUser(_, _));

  // Kick off the 1st start.
  TestFuture<CryptohomeStatus> on_done_result;
  service_.Start(user_, on_done_result.GetCallback());
  ASSERT_THAT(on_done_result.IsReady(), IsFalse());

  // Kick off the 2nd start.
  TestFuture<CryptohomeStatus> second_on_done_result;
  std::string another_user = "another_name";
  service_.Start(user_, second_on_done_result.GetCallback());
  ASSERT_THAT(second_on_done_result.IsReady(), IsTrue());
  ASSERT_THAT(second_on_done_result.Get()->local_legacy_error(),
              Eq(user_data_auth::CRYPTOHOME_ERROR_FINGERPRINT_DENIED));
}

TEST_F(FingerprintAuthBlockServiceTest, VerifySimpleSuccess) {
  // Capture the callbacks from the fingerprint manager.
  FingerprintManager::SignalCallback signal_callback;
  EXPECT_CALL(fp_manager_, SetSignalCallback(_))
      .WillOnce(SaveSignalCallback{&signal_callback});
  FingerprintManager::StartSessionCallback start_session_callback;
  EXPECT_CALL(fp_manager_, StartAuthSessionAsyncForUser(_, _))
      .WillOnce(SaveStartSessionCallback{&start_session_callback});

  // Kick off the start.
  TestFuture<CryptohomeStatus> start_result;
  service_.Start(user_, start_result.GetCallback());
  // The |start_result| should only be triggered after we execute the
  // callbacks from the fingerprint manager.
  ASSERT_THAT(start_result.IsReady(), IsFalse());
  std::move(start_session_callback).Run(true);
  ASSERT_THAT(start_result.IsReady(), IsTrue());
  ASSERT_THAT(start_result.Get(), IsOk());
  // Simulate a success scan.
  signal_callback.Run(FingerprintScanStatus::SUCCESS);

  // Kick off the verify. Because there was a success scan, the callback
  // shall return immediately with success result.
  TestFuture<CryptohomeStatus> verify_result;
  service_.Verify(verify_result.GetCallback());
  ASSERT_THAT(verify_result.IsReady(), IsTrue());
  ASSERT_THAT(verify_result.Get(), IsOk());
  // Check the signal sender has been called.
  ASSERT_EQ(
      result_,
      user_data_auth::FingerprintScanResult::FINGERPRINT_SCAN_RESULT_SUCCESS);
}

TEST_F(FingerprintAuthBlockServiceTest, VerifySimpleFailure) {
  // Without a previous Start() while kicking off the verify,
  // the callback shall return immediately with a failure.
  TestFuture<CryptohomeStatus> verify_result;
  service_.Verify(verify_result.GetCallback());
  ASSERT_THAT(verify_result.IsReady(), IsTrue());
  EXPECT_THAT(verify_result.Get()->local_legacy_error(),
              Eq(user_data_auth::CRYPTOHOME_ERROR_FINGERPRINT_ERROR_INTERNAL));
}

TEST_F(FingerprintAuthBlockServiceTest, VerifyNoScanFailure) {
  // Capture the callbacks from the fingerprint manager.
  FingerprintManager::SignalCallback signal_callback;
  EXPECT_CALL(fp_manager_, SetSignalCallback(_))
      .WillOnce(SaveSignalCallback{&signal_callback});
  FingerprintManager::StartSessionCallback start_session_callback;
  EXPECT_CALL(fp_manager_, StartAuthSessionAsyncForUser(_, _))
      .WillOnce(SaveStartSessionCallback{&start_session_callback});

  // Kick off the start.
  TestFuture<CryptohomeStatus> start_result;
  service_.Start(user_, start_result.GetCallback());
  // The |start_result| should only be triggered after we execute the
  // callbacks from the fingerprint manager.
  ASSERT_THAT(start_result.IsReady(), IsFalse());
  std::move(start_session_callback).Run(true);
  ASSERT_THAT(start_result.IsReady(), IsTrue());
  ASSERT_THAT(start_result.Get(), IsOk());

  // Kick off the verify without a scan result.
  TestFuture<CryptohomeStatus> verify_result;
  service_.Verify(verify_result.GetCallback());
  ASSERT_THAT(verify_result.IsReady(), IsTrue());
  EXPECT_THAT(verify_result.Get()->local_legacy_error(),
              Eq(user_data_auth::CRYPTOHOME_ERROR_FINGERPRINT_DENIED));
}

TEST_F(FingerprintAuthBlockServiceTest, VerifyAfterTerminateFailure) {
  // Capture the callbacks from the fingerprint manager.
  FingerprintManager::SignalCallback signal_callback;
  EXPECT_CALL(fp_manager_, SetSignalCallback(_))
      .WillOnce(SaveSignalCallback{&signal_callback});
  FingerprintManager::StartSessionCallback start_session_callback;
  EXPECT_CALL(fp_manager_, StartAuthSessionAsyncForUser(_, _))
      .WillOnce(SaveStartSessionCallback{&start_session_callback});

  // Kick off the start.
  TestFuture<CryptohomeStatus> start_result;
  service_.Start(user_, start_result.GetCallback());
  // The |start_result| should only be triggered after we execute the
  // callbacks from the fingerprint manager.
  ASSERT_THAT(start_result.IsReady(), IsFalse());
  std::move(start_session_callback).Run(true);
  ASSERT_THAT(start_result.IsReady(), IsTrue());
  ASSERT_THAT(start_result.Get(), IsOk());

  // Terminate the service session.
  EXPECT_CALL(fp_manager_, EndAuthSession());
  service_.Terminate();

  // Kick off the verify.
  TestFuture<CryptohomeStatus> verify_result;
  service_.Verify(verify_result.GetCallback());
  ASSERT_THAT(verify_result.IsReady(), IsTrue());
  EXPECT_THAT(verify_result.Get()->local_legacy_error(),
              Eq(user_data_auth::CRYPTOHOME_ERROR_FINGERPRINT_ERROR_INTERNAL));
}

TEST_F(FingerprintAuthBlockServiceTest, VerifyRetryFailure) {
  // Capture the callbacks from the fingerprint manager.
  FingerprintManager::SignalCallback signal_callback;
  EXPECT_CALL(fp_manager_, SetSignalCallback(_))
      .WillOnce(SaveSignalCallback{&signal_callback});
  FingerprintManager::StartSessionCallback start_session_callback;
  EXPECT_CALL(fp_manager_, StartAuthSessionAsyncForUser(_, _))
      .WillOnce(SaveStartSessionCallback{&start_session_callback});

  // Kick off the start.
  TestFuture<CryptohomeStatus> start_result;
  service_.Start(user_, start_result.GetCallback());
  // The |start_result| should only be triggered after we execute the
  // callbacks from the fingerprint manager.
  ASSERT_THAT(start_result.IsReady(), IsFalse());
  std::move(start_session_callback).Run(true);
  ASSERT_THAT(start_result.IsReady(), IsTrue());
  ASSERT_THAT(start_result.Get(), IsOk());
  // Simulate a retry-able scan.
  signal_callback.Run(FingerprintScanStatus::FAILED_RETRY_ALLOWED);

  // Kick off the verify. Because there was a success scan, the callback
  // shall return immediately with success result.
  TestFuture<CryptohomeStatus> verify_result;
  service_.Verify(verify_result.GetCallback());
  ASSERT_THAT(verify_result.IsReady(), IsTrue());
  EXPECT_THAT(verify_result.Get()->local_legacy_error(),
              Eq(user_data_auth::CRYPTOHOME_ERROR_FINGERPRINT_RETRY_REQUIRED));
}

TEST_F(FingerprintAuthBlockServiceTest, VerifyRetryDeniedFailure) {
  // Capture the callbacks from the fingerprint manager.
  FingerprintManager::SignalCallback signal_callback;
  EXPECT_CALL(fp_manager_, SetSignalCallback(_))
      .WillOnce(SaveSignalCallback{&signal_callback});
  FingerprintManager::StartSessionCallback start_session_callback;
  EXPECT_CALL(fp_manager_, StartAuthSessionAsyncForUser(_, _))
      .WillOnce(SaveStartSessionCallback{&start_session_callback});

  // Kick off the start.
  TestFuture<CryptohomeStatus> start_result;
  service_.Start(user_, start_result.GetCallback());
  // The |start_result| should only be triggered after we execute the
  // callbacks from the fingerprint manager.
  ASSERT_THAT(start_result.IsReady(), IsFalse());
  std::move(start_session_callback).Run(true);
  ASSERT_THAT(start_result.IsReady(), IsTrue());
  ASSERT_THAT(start_result.Get(), IsOk());
  // Simulate a retry-able scan.
  signal_callback.Run(FingerprintScanStatus::FAILED_RETRY_NOT_ALLOWED);

  // Kick off the verify. Because there was a success scan, the callback
  // shall return immediately with success result.
  TestFuture<CryptohomeStatus> verify_result;
  service_.Verify(verify_result.GetCallback());
  ASSERT_THAT(verify_result.IsReady(), IsTrue());
  EXPECT_THAT(verify_result.Get()->local_legacy_error(),
              Eq(user_data_auth::CRYPTOHOME_ERROR_FINGERPRINT_DENIED));
}

TEST_F(FingerprintAuthBlockServiceTest, ScanResultSignalCallbackSuccess) {
  // Capture the callbacks from the fingerprint manager.
  FingerprintManager::SignalCallback signal_callback;
  EXPECT_CALL(fp_manager_, SetSignalCallback(_))
      .WillOnce(SaveSignalCallback{&signal_callback});
  FingerprintManager::StartSessionCallback start_session_callback;
  EXPECT_CALL(fp_manager_, StartAuthSessionAsyncForUser(_, _))
      .WillOnce(SaveStartSessionCallback{&start_session_callback});
  result_ = user_data_auth::FINGERPRINT_SCAN_RESULT_LOCKOUT;

  // Kick off the start.
  TestFuture<CryptohomeStatus> start_result;
  service_.Start(user_, start_result.GetCallback());
  // The |start_result| should only be triggered after we execute the
  // callbacks from the fingerprint manager.
  ASSERT_THAT(start_result.IsReady(), IsFalse());
  std::move(start_session_callback).Run(true);
  ASSERT_THAT(start_result.IsReady(), IsTrue());
  ASSERT_THAT(start_result.Get(), IsOk());

  // Simulate multiple scan results. And Check the outgoing signal. The callback
  // shall return corresponding outgoing signal values.
  signal_callback.Run(FingerprintScanStatus::FAILED_RETRY_ALLOWED);
  ASSERT_EQ(result_, user_data_auth::FINGERPRINT_SCAN_RESULT_RETRY);
  signal_callback.Run(FingerprintScanStatus::SUCCESS);
  ASSERT_EQ(result_, user_data_auth::FINGERPRINT_SCAN_RESULT_SUCCESS);
  signal_callback.Run(FingerprintScanStatus::FAILED_RETRY_NOT_ALLOWED);
  ASSERT_EQ(result_, user_data_auth::FINGERPRINT_SCAN_RESULT_LOCKOUT);
}

}  // namespace

}  // namespace cryptohome
