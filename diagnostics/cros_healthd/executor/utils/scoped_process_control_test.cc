// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/executor/utils/scoped_process_control.h"

#include <base/functional/callback_helpers.h>
#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <mojo/public/cpp/bindings/callback_helpers.h>

#include "diagnostics/cros_healthd/executor/utils/fake_process_control.h"
#include "diagnostics/cros_healthd/utils/callback_barrier.h"

namespace diagnostics {
namespace {

using ::testing::_;
using ::testing::Return;

class ScopedProcessControlTest : public testing::Test {
 public:
  ScopedProcessControlTest(const ScopedProcessControlTest&) = delete;
  ScopedProcessControlTest& operator=(const ScopedProcessControlTest&) = delete;

 protected:
  ScopedProcessControlTest() = default;

  FakeProcessControl fake_process_control_;

 private:
  base::test::TaskEnvironment task_environment_;
};

TEST_F(ScopedProcessControlTest, RunOneCallbackOnOutOfScope) {
  base::test::TestFuture<void> future;
  {
    ScopedProcessControl scoped_process_control;
    fake_process_control_.BindReceiver(
        scoped_process_control.BindNewPipeAndPassReceiver());
    scoped_process_control.AddOnTerminateCallback(
        base::ScopedClosureRunner(future.GetCallback()));
  }
  EXPECT_TRUE(future.Wait());
  EXPECT_EQ(fake_process_control_.return_code(), 143);
  fake_process_control_.receiver().FlushForTesting();
  EXPECT_FALSE(fake_process_control_.IsConnected());
}

TEST_F(ScopedProcessControlTest, RunMultipleCallbacksOnOutOfScope) {
  base::test::TestFuture<bool> future;
  {
    CallbackBarrier barrier(future.GetCallback());
    ScopedProcessControl scoped_process_control;
    fake_process_control_.BindReceiver(
        scoped_process_control.BindNewPipeAndPassReceiver());
    scoped_process_control.AddOnTerminateCallback(
        base::ScopedClosureRunner(barrier.CreateDependencyClosure()));
    scoped_process_control.AddOnTerminateCallback(
        base::ScopedClosureRunner(barrier.CreateDependencyClosure()));
    scoped_process_control.AddOnTerminateCallback(
        base::ScopedClosureRunner(barrier.CreateDependencyClosure()));
  }
  EXPECT_TRUE(future.Get());
  EXPECT_EQ(fake_process_control_.return_code(), 143);
  fake_process_control_.receiver().FlushForTesting();
  EXPECT_FALSE(fake_process_control_.IsConnected());
}

TEST_F(ScopedProcessControlTest, RunAllCallbacksOnReset) {
  base::test::TestFuture<void> future;
  ScopedProcessControl scoped_process_control;
  fake_process_control_.BindReceiver(
      scoped_process_control.BindNewPipeAndPassReceiver());
  scoped_process_control.AddOnTerminateCallback(
      base::ScopedClosureRunner(future.GetCallback()));
  scoped_process_control.Reset();
  EXPECT_TRUE(future.Wait());
  EXPECT_EQ(fake_process_control_.return_code(), 143);
  fake_process_control_.receiver().FlushForTesting();
  EXPECT_FALSE(fake_process_control_.IsConnected());
}

TEST_F(ScopedProcessControlTest, ResetSuccessfullyIfNoRemoteBound) {
  base::test::TestFuture<void> future;
  ScopedProcessControl scoped_process_control;
  scoped_process_control.AddOnTerminateCallback(
      base::ScopedClosureRunner(future.GetCallback()));
  scoped_process_control.Reset();
  EXPECT_TRUE(future.Wait());
}

TEST_F(ScopedProcessControlTest, AddCallbackAfterCallbacksCalled) {
  ScopedProcessControl scoped_process_control;
  fake_process_control_.BindReceiver(
      scoped_process_control.BindNewPipeAndPassReceiver());
  {
    base::test::TestFuture<void> future;
    scoped_process_control.AddOnTerminateCallback(
        base::ScopedClosureRunner(future.GetCallback()));
    fake_process_control_.SetReturnCode(0);
    EXPECT_TRUE(future.Wait());
  }
  fake_process_control_.receiver().FlushForTesting();
  EXPECT_TRUE(fake_process_control_.IsConnected());
  {
    base::test::TestFuture<void> future;
    scoped_process_control.AddOnTerminateCallback(
        base::ScopedClosureRunner(future.GetCallback()));
    EXPECT_TRUE(future.Wait());
  }
  fake_process_control_.receiver().FlushForTesting();
  EXPECT_TRUE(fake_process_control_.IsConnected());
}

}  // namespace
}  // namespace diagnostics
