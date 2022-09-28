// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_blocks/fp_service.h"

#include <string>
#include <utility>

#include <base/bind.h>
#include <base/task/sequenced_task_runner.h>
#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <base/threading/sequenced_task_runner_handle.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libhwsec-foundation/error/testing_helper.h>

#include "cryptohome/mock_fingerprint_manager.h"

namespace cryptohome {
namespace {

using base::test::TestFuture;
using hwsec_foundation::error::testing::IsOk;

using ::testing::_;
using ::testing::Eq;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::StrictMock;

// Functor for saving off a fingerprint result callback into a given argument.
// Useful for mocking out SetAuthScanDoneCallback to capture the parameter.
struct SaveResultCallback {
  void operator()(FingerprintManager::ResultCallback callback) {
    *captured_callback = std::move(callback);
  }
  FingerprintManager::ResultCallback* captured_callback;
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

TEST_F(NullFingerprintAuthBlockServiceTest, NullStartScanFails) {
  auto service = FingerprintAuthBlockService::MakeNullService();
  std::string dummy_username = "dummy";

  TestFuture<CryptohomeStatus> on_done_result;
  service->Scan(on_done_result.GetCallback());

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
            base::Unretained(this))) {}

  void SetUp() override {
    auto status = service_.Start(user_);
    ASSERT_TRUE(status.ok());
  }

  void TearDown() override {
    EXPECT_CALL(fp_manager_, EndAuthSession());
    service_.Terminate();
  }

 protected:
  FingerprintManager* GetFingerprintManager() { return &fp_manager_; }

  StrictMock<MockFingerprintManager> fp_manager_;
  FingerprintAuthBlockService service_;
  std::string user_ = "dummy_user";
};

TEST_F(FingerprintAuthBlockServiceTest, StartFailureWithDifferentUser) {
  std::string another_username = "another_name";
  auto status = service_.Start(another_username);
  ASSERT_TRUE(!status.ok());
  EXPECT_THAT(status->local_legacy_error(),
              Eq(user_data_auth::CRYPTOHOME_ERROR_FINGERPRINT_DENIED));
}

TEST_F(FingerprintAuthBlockServiceTest, StartSuccessWithSameUser) {
  auto status = service_.Start(user_);
  ASSERT_TRUE(status.ok());
}

TEST_F(FingerprintAuthBlockServiceTest, ScanSuccess) {
  // Capture the callbacks from the fingerprint manager.
  FingerprintManager::ResultCallback result_callback;
  EXPECT_CALL(fp_manager_, SetAuthScanDoneCallback(_))
      .WillOnce(SaveResultCallback{&result_callback});
  FingerprintManager::StartSessionCallback start_session_callback;
  EXPECT_CALL(fp_manager_, StartAuthSessionAsyncForUser(_, _))
      .WillOnce(SaveStartSessionCallback{&start_session_callback});
  EXPECT_CALL(fp_manager_, EndAuthSession());

  // Kick off the scan.
  TestFuture<CryptohomeStatus> on_done_result;
  service_.Scan(on_done_result.GetCallback());
  // The on_done should only be triggered after we execute the callback from the
  // fingerprint manager.
  ASSERT_THAT(on_done_result.IsReady(), IsFalse());
  std::move(start_session_callback).Run(true);
  std::move(result_callback).Run(FingerprintScanStatus::SUCCESS);
  ASSERT_THAT(on_done_result.IsReady(), IsTrue());
  ASSERT_THAT(on_done_result.Get(), IsOk());
}

TEST_F(FingerprintAuthBlockServiceTest, ScanSessionStartFailure) {
  // Capture the callbacks from the fingerprint manager.
  FingerprintManager::StartSessionCallback start_session_callback;
  EXPECT_CALL(fp_manager_, StartAuthSessionAsyncForUser(_, _))
      .WillOnce(SaveStartSessionCallback{&start_session_callback});

  // Kick off the scan.
  TestFuture<CryptohomeStatus> on_done_result;
  service_.Scan(on_done_result.GetCallback());

  // The on_done should only be triggered after we execute the callback from the
  // fingerprint manager.
  ASSERT_THAT(on_done_result.IsReady(), IsFalse());
  std::move(start_session_callback).Run(false);
  ASSERT_THAT(on_done_result.IsReady(), IsTrue());
  EXPECT_THAT(on_done_result.Get()->local_legacy_error(),
              Eq(user_data_auth::CRYPTOHOME_ERROR_FINGERPRINT_ERROR_INTERNAL));
}

TEST_F(FingerprintAuthBlockServiceTest, ScanSuccessWithBadScanResult) {
  // Capture the callbacks from the fingerprint manager.
  FingerprintManager::ResultCallback result_callback;
  EXPECT_CALL(fp_manager_, SetAuthScanDoneCallback(_))
      .WillOnce(SaveResultCallback{&result_callback});
  FingerprintManager::StartSessionCallback start_session_callback;
  EXPECT_CALL(fp_manager_, StartAuthSessionAsyncForUser(_, _))
      .WillOnce(SaveStartSessionCallback{&start_session_callback});
  EXPECT_CALL(fp_manager_, EndAuthSession());
  std::string dummy_username = "dummy";

  // Kick off the scan.
  TestFuture<CryptohomeStatus> on_done_result;
  service_.Scan(on_done_result.GetCallback());
  // The |on_done_result| should only be triggered after we execute the callback
  // from the fingerprint manager.
  ASSERT_THAT(on_done_result.IsReady(), IsFalse());
  std::move(start_session_callback).Run(true);
  std::move(result_callback).Run(FingerprintScanStatus::FAILED_RETRY_ALLOWED);
  ASSERT_THAT(on_done_result.IsReady(), IsTrue());
  ASSERT_THAT(on_done_result.Get(), IsOk());
}

TEST_F(FingerprintAuthBlockServiceTest, ScanFailureAfterFatalError) {
  // Capture the callbacks from the fingerprint manager.
  FingerprintManager::ResultCallback result_callback;
  EXPECT_CALL(fp_manager_, SetAuthScanDoneCallback(_))
      .WillOnce(SaveResultCallback{&result_callback});
  FingerprintManager::StartSessionCallback start_session_callback;
  EXPECT_CALL(fp_manager_, StartAuthSessionAsyncForUser(_, _))
      .WillOnce(SaveStartSessionCallback{&start_session_callback});
  EXPECT_CALL(fp_manager_, EndAuthSession());

  {
    // Kick off the first success start scan.
    TestFuture<CryptohomeStatus> on_done_result;
    service_.Scan(on_done_result.GetCallback());
    // The |on_done_result| should only be triggered after we execute the
    // callback from the fingerprint manager.
    ASSERT_THAT(on_done_result.IsReady(), IsFalse());
    std::move(start_session_callback).Run(true);
    std::move(result_callback)
        .Run(FingerprintScanStatus::FAILED_RETRY_NOT_ALLOWED);
    ASSERT_THAT(on_done_result.IsReady(), IsTrue());
    ASSERT_THAT(on_done_result.Get(), IsOk());
  }

  {
    // Kick off the 2nd start scan and expect failure immediately.
    TestFuture<CryptohomeStatus> on_done_result;
    service_.Scan(on_done_result.GetCallback());
    ASSERT_THAT(on_done_result.IsReady(), IsTrue());
    EXPECT_THAT(on_done_result.Get()->local_legacy_error(),
                Eq(user_data_auth::CRYPTOHOME_ERROR_FINGERPRINT_DENIED));
  }
}

TEST_F(FingerprintAuthBlockServiceTest, VerifySimpleSuccess) {
  // Capture the callbacks from the fingerprint manager.
  FingerprintManager::ResultCallback result_callback;
  EXPECT_CALL(fp_manager_, SetAuthScanDoneCallback(_))
      .WillOnce(SaveResultCallback{&result_callback});
  FingerprintManager::StartSessionCallback start_session_callback;
  EXPECT_CALL(fp_manager_, StartAuthSessionAsyncForUser(_, _))
      .WillOnce(SaveStartSessionCallback{&start_session_callback});
  EXPECT_CALL(fp_manager_, EndAuthSession());

  // Kick off the scan.
  TestFuture<CryptohomeStatus> scan_result;
  service_.Scan(scan_result.GetCallback());
  // The |scan_result| should only be triggered after we execute the
  // callbacks from the fingerprint manager.
  ASSERT_THAT(scan_result.IsReady(), IsFalse());
  std::move(start_session_callback).Run(true);
  std::move(result_callback).Run(FingerprintScanStatus::SUCCESS);
  ASSERT_THAT(scan_result.IsReady(), IsTrue());
  ASSERT_THAT(scan_result.Get(), IsOk());

  // Kick off the verify. Because there was a success scan, the callback
  // shall return immediately with success result.
  TestFuture<CryptohomeStatus> verify_result;
  service_.Verify(verify_result.GetCallback());
  ASSERT_THAT(verify_result.IsReady(), IsTrue());
  ASSERT_THAT(verify_result.Get(), IsOk());
}

TEST_F(FingerprintAuthBlockServiceTest, VerifySimpleFailure) {
  // Without a previous Scan() while kicking off the verify,
  // the callback shall return immediately with failure result.
  TestFuture<CryptohomeStatus> verify_result;
  service_.Verify(verify_result.GetCallback());
  ASSERT_THAT(verify_result.IsReady(), IsTrue());
  EXPECT_THAT(verify_result.Get()->local_legacy_error(),
              Eq(user_data_auth::CRYPTOHOME_ERROR_FINGERPRINT_DENIED));
}

TEST_F(FingerprintAuthBlockServiceTest, VerifyRetryFailure) {
  // Capture the callbacks from the fingerprint manager.
  FingerprintManager::ResultCallback result_callback;
  EXPECT_CALL(fp_manager_, SetAuthScanDoneCallback(_))
      .WillOnce(SaveResultCallback{&result_callback});
  FingerprintManager::StartSessionCallback start_session_callback;
  EXPECT_CALL(fp_manager_, StartAuthSessionAsyncForUser(_, _))
      .WillOnce(SaveStartSessionCallback{&start_session_callback});
  EXPECT_CALL(fp_manager_, EndAuthSession());

  // Kick off the scan.
  TestFuture<CryptohomeStatus> scan_result;
  service_.Scan(scan_result.GetCallback());
  // The |scan_result| should only be triggered after we execute the
  // callback from the fingerprint manager.
  ASSERT_THAT(scan_result.IsReady(), IsFalse());
  std::move(start_session_callback).Run(true);
  std::move(result_callback).Run(FingerprintScanStatus::FAILED_RETRY_ALLOWED);
  ASSERT_THAT(scan_result.IsReady(), IsTrue());
  ASSERT_THAT(scan_result.Get(), IsOk());

  // Kick off the verify. Because there was a success scan, the callback
  // shall return immediately with retry result.
  TestFuture<CryptohomeStatus> verify_result;
  service_.Verify(verify_result.GetCallback());
  ASSERT_THAT(verify_result.IsReady(), IsTrue());
  EXPECT_THAT(verify_result.Get()->local_legacy_error(),
              Eq(user_data_auth::CRYPTOHOME_ERROR_FINGERPRINT_RETRY_REQUIRED));
}

TEST_F(FingerprintAuthBlockServiceTest, VerifyFatalFailure) {
  // Capture the callbacks from the fingerprint manager.
  FingerprintManager::ResultCallback result_callback;
  EXPECT_CALL(fp_manager_, SetAuthScanDoneCallback(_))
      .WillOnce(SaveResultCallback{&result_callback});
  FingerprintManager::StartSessionCallback start_session_callback;
  EXPECT_CALL(fp_manager_, StartAuthSessionAsyncForUser(_, _))
      .WillOnce(SaveStartSessionCallback{&start_session_callback});
  EXPECT_CALL(fp_manager_, EndAuthSession());

  // Kick off the scan.
  TestFuture<CryptohomeStatus> scan_result;
  service_.Scan(scan_result.GetCallback());
  // The |scan_result| should only be triggered after we execute the
  // callback from the fingerprint manager.
  ASSERT_THAT(scan_result.IsReady(), IsFalse());
  std::move(start_session_callback).Run(true);
  std::move(result_callback)
      .Run(FingerprintScanStatus::FAILED_RETRY_NOT_ALLOWED);
  ASSERT_THAT(scan_result.IsReady(), IsTrue());
  ASSERT_THAT(scan_result.Get(), IsOk());

  // Kick off the verify. The callback shall return immediately
  // with not retry-able result.
  TestFuture<CryptohomeStatus> verify_result;
  service_.Verify(verify_result.GetCallback());
  ASSERT_THAT(verify_result.IsReady(), IsTrue());
  EXPECT_THAT(verify_result.Get()->local_legacy_error(),
              Eq(user_data_auth::CRYPTOHOME_ERROR_FINGERPRINT_DENIED));
}

TEST_F(FingerprintAuthBlockServiceTest, RetryLimitExceeded) {
  // Capture the callbacks from the fingerprint manager.
  FingerprintManager::ResultCallback result_callback;
  EXPECT_CALL(fp_manager_, SetAuthScanDoneCallback(_))
      .WillRepeatedly(SaveResultCallback{&result_callback});
  FingerprintManager::StartSessionCallback start_session_callback;
  EXPECT_CALL(fp_manager_, StartAuthSessionAsyncForUser(_, _))
      .WillRepeatedly(SaveStartSessionCallback{&start_session_callback});
  EXPECT_CALL(fp_manager_, EndAuthSession()).Times(kMaxFingerprintRetries);

  // Do the scan until the retry limit is reached.
  for (int i = 0; i < kMaxFingerprintRetries; ++i) {
    TestFuture<CryptohomeStatus> scan_result;
    service_.Scan(scan_result.GetCallback());
    ASSERT_THAT(scan_result.IsReady(), IsFalse());
    std::move(start_session_callback).Run(true);
    std::move(result_callback).Run(FingerprintScanStatus::FAILED_RETRY_ALLOWED);
    ASSERT_THAT(scan_result.IsReady(), IsTrue());
    ASSERT_THAT(scan_result.Get(), IsOk());
  }

  // Kick off the verify. The callback shall return immediately with retry
  // denied result.
  TestFuture<CryptohomeStatus> verify_result;
  service_.Verify(verify_result.GetCallback());
  ASSERT_THAT(verify_result.IsReady(), IsTrue());
  EXPECT_THAT(verify_result.Get()->local_legacy_error(),
              Eq(user_data_auth::CRYPTOHOME_ERROR_FINGERPRINT_DENIED));
}

}  // namespace

}  // namespace cryptohome
