// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_blocks/fp_service.h"

#include <base/task/sequenced_task_runner.h>
#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <base/threading/sequenced_task_runner_handle.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libhwsec-foundation/error/testing_helper.h>

#include "base/bind.h"
#include "cryptohome/mock_fingerprint_manager.h"

namespace cryptohome {
namespace {

using base::test::TestFuture;
using hwsec_foundation::error::testing::IsOk;

using ::testing::_;
using ::testing::Eq;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::SaveArg;
using ::testing::StrictMock;

// Base test fixture which sets up the environment.
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

 protected:
  FingerprintManager* GetFingerprintManager() { return &fp_manager_; }

  StrictMock<MockFingerprintManager> fp_manager_;
  FingerprintAuthBlockService service_;
};

TEST_F(FingerprintAuthBlockServiceTest, SimpleSuccess) {
  // Capture the result callback from the fingerprint manager.
  FingerprintManager::ResultCallback result_callback;
  EXPECT_CALL(fp_manager_, SetAuthScanDoneCallback(_))
      .WillOnce(SaveArg<0>(&result_callback));

  // Kick off the verify.
  TestFuture<CryptohomeStatus> on_done_result;
  service_.Verify(on_done_result.GetCallback());

  // The on_done should only be triggered after we execute the callback from the
  // fingerprint manager.
  ASSERT_THAT(on_done_result.IsReady(), IsFalse());
  result_callback.Run(FingerprintScanStatus::SUCCESS);
  ASSERT_THAT(on_done_result.IsReady(), IsTrue());
  ASSERT_THAT(on_done_result.Get(), IsOk());
}

TEST_F(FingerprintAuthBlockServiceTest, SimpleFailure) {
  // Capture the result callback from the fingerprint manager.
  FingerprintManager::ResultCallback result_callback;
  EXPECT_CALL(fp_manager_, SetAuthScanDoneCallback(_))
      .WillOnce(SaveArg<0>(&result_callback));

  // Kick off the verify.
  TestFuture<CryptohomeStatus> on_done_result;
  service_.Verify(on_done_result.GetCallback());

  // The on_done should only be triggered after we execute the callback from the
  // fingerprint manager.
  ASSERT_THAT(on_done_result.IsReady(), IsFalse());
  result_callback.Run(FingerprintScanStatus::FAILED_RETRY_NOT_ALLOWED);
  ASSERT_THAT(on_done_result.IsReady(), IsTrue());
  EXPECT_THAT(on_done_result.Get()->local_legacy_error(),
              Eq(user_data_auth::CRYPTOHOME_ERROR_FINGERPRINT_DENIED));
}

TEST_F(FingerprintAuthBlockServiceTest, FailureWithRetries) {
  // First attempt, resulting in a fail-but-you-can-retry.
  {
    // Capture the result callback from the fingerprint manager.
    FingerprintManager::ResultCallback result_callback;
    EXPECT_CALL(fp_manager_, SetAuthScanDoneCallback(_))
        .WillOnce(SaveArg<0>(&result_callback));

    // Kick off the verify.
    TestFuture<CryptohomeStatus> on_done_result;
    service_.Verify(on_done_result.GetCallback());

    // The on_done should only be triggered after we execute the callback from
    // the fingerprint manager.
    ASSERT_THAT(on_done_result.IsReady(), IsFalse());
    result_callback.Run(FingerprintScanStatus::FAILED_RETRY_ALLOWED);
    ASSERT_THAT(on_done_result.IsReady(), IsTrue());
    EXPECT_THAT(
        on_done_result.Get()->local_legacy_error(),
        Eq(user_data_auth::CRYPTOHOME_ERROR_FINGERPRINT_RETRY_REQUIRED));
  }

  // Second attempt, should now succeed.
  {
    // Capture the result callback from the fingerprint manager.
    FingerprintManager::ResultCallback result_callback;
    EXPECT_CALL(fp_manager_, SetAuthScanDoneCallback(_))
        .WillOnce(SaveArg<0>(&result_callback));

    // Kick off the verify.
    TestFuture<CryptohomeStatus> on_done_result;
    service_.Verify(on_done_result.GetCallback());

    // The on_done should only be triggered after we execute the callback from
    // the fingerprint manager.
    ASSERT_THAT(on_done_result.IsReady(), IsFalse());
    result_callback.Run(FingerprintScanStatus::SUCCESS);
    ASSERT_THAT(on_done_result.IsReady(), IsTrue());
    ASSERT_THAT(on_done_result.Get(), IsOk());
  }
}

}  // namespace
}  // namespace cryptohome
