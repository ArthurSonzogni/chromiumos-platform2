// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/memory_and_cpu/urandom_v2.h"

#include <memory>
#include <string>
#include <utility>

#include <base/functional/callback_helpers.h>
#include <base/test/bind.h>
#include <base/test/task_environment.h>
#include <base/time/time.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "diagnostics/cros_healthd/executor/executor.h"
#include "diagnostics/cros_healthd/executor/utils/fake_process_control.h"
#include "diagnostics/cros_healthd/routines/base_routine_control.h"
#include "diagnostics/cros_healthd/routines/memory_and_cpu/constants.h"
#include "diagnostics/cros_healthd/routines/routine_observer_for_testing.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "diagnostics/mojom/public/cros_healthd_routines.mojom.h"

namespace diagnostics {
namespace {

namespace mojom = ash::cros_healthd::mojom;

using ::testing::_;

class UrandomRoutineV2Test : public testing::Test {
 public:
  UrandomRoutineV2Test(const UrandomRoutineV2Test&) = delete;
  UrandomRoutineV2Test& operator=(const UrandomRoutineV2Test&) = delete;

 protected:
  UrandomRoutineV2Test() = default;

  void SetUp() {
    EXPECT_CALL(*mock_context_.mock_executor(), RunUrandom(_, _, _))
        .WillRepeatedly(
            [=, this](
                base::TimeDelta exec_duration,
                mojo::PendingReceiver<ash::cros_healthd::mojom::ProcessControl>
                    receiver,
                Executor::RunUrandomCallback callback) {
              fake_process_control_.BindReceiver(std::move(receiver));
              received_exec_duration_ = exec_duration;
              received_callback_ = std::move(callback);
            });
    routine_ = std::make_unique<UrandomRoutineV2>(
        &mock_context_, mojom::UrandomRoutineArgument::New(
                            /*exec_duration=*/std::nullopt));
  }

  mojom::RoutineStatePtr RunRoutineAndWaitForExit(bool passed) {
    base::RunLoop run_loop;
    routine_->SetOnExceptionCallback(
        base::BindOnce([](uint32_t error, const std::string& reason) {
          ADD_FAILURE() << "An exception has occurred when it shouldn't have.";
        }));
    auto observer =
        std::make_unique<RoutineObserverForTesting>(run_loop.QuitClosure());
    routine_->SetObserver(observer->receiver_.BindNewPipeAndPassRemote());
    routine_->Start();
    FinishUrandomDelegate(passed);
    run_loop.Run();
    return std::move(observer->state_);
  }

  void RunRoutineAndWaitForException() {
    base::RunLoop run_loop;
    routine_->SetOnExceptionCallback(
        base::IgnoreArgs<uint32_t, const std::string&>(run_loop.QuitClosure()));
    routine_->Start();
    run_loop.Run();
  }

  // Sets the mock executor response by running the callback and returning
  // passed or failed based on input.
  void FinishUrandomDelegate(bool passed) {
    std::move(received_callback_).Run(passed);
  }

  std::unique_ptr<BaseRoutineControl> routine_;
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  MockContext mock_context_;
  FakeProcessControl fake_process_control_;
  base::TimeDelta received_exec_duration_ = base::Seconds(0);
  Executor::RunUrandomCallback received_callback_;
};

// Test that the routine can run successfully.
TEST_F(UrandomRoutineV2Test, RoutineSuccess) {
  mojom::RoutineStatePtr result = RunRoutineAndWaitForExit(true);
  EXPECT_EQ(result->percentage, 100);
  EXPECT_TRUE(result->state_union->is_finished());
  EXPECT_TRUE(result->state_union->get_finished()->has_passed);
}

// Test that the routine can fail successfully.
TEST_F(UrandomRoutineV2Test, RoutineFailure) {
  mojom::RoutineStatePtr result = RunRoutineAndWaitForExit(false);
  EXPECT_EQ(result->percentage, 100);
  EXPECT_TRUE(result->state_union->is_finished());
  EXPECT_FALSE(result->state_union->get_finished()->has_passed);
}

// Test that the routine defaults to 60 seconds if no duration is provided.
TEST_F(UrandomRoutineV2Test, DefaultTestSeconds) {
  RunRoutineAndWaitForExit(true);
  EXPECT_EQ(received_exec_duration_, base::Seconds(60));
}

// Test that the routine can run with custom time.
TEST_F(UrandomRoutineV2Test, CustomTestSeconds) {
  routine_ = std::make_unique<UrandomRoutineV2>(
      &mock_context_, mojom::UrandomRoutineArgument::New(
                          /*exec_duration=*/base::Seconds(20)));
  RunRoutineAndWaitForExit(true);
  EXPECT_EQ(received_exec_duration_, base::Seconds(20));
}

// Test that the routine defaults to minimum running time (1 second) if invalid
// duration is provided.
TEST_F(UrandomRoutineV2Test, InvalidTestSecondsFallbackToMinimumDefault) {
  routine_ = std::make_unique<UrandomRoutineV2>(
      &mock_context_, mojom::UrandomRoutineArgument::New(
                          /*exec_duration=*/base::Seconds(0)));
  RunRoutineAndWaitForExit(true);
  EXPECT_EQ(received_exec_duration_, base::Seconds(1));
}

// Test that the routine can report progress correctly.
TEST_F(UrandomRoutineV2Test, IncrementalProgress) {
  routine_ = std::make_unique<UrandomRoutineV2>(
      &mock_context_, mojom::UrandomRoutineArgument::New(
                          /*exec_duration=*/base::Seconds(60)));
  routine_->SetOnExceptionCallback(
      base::BindOnce([](uint32_t error, const std::string& reason) {
        ADD_FAILURE() << "An exception has occurred when it shouldn't have.";
      }));
  auto observer =
      std::make_unique<RoutineObserverForTesting>(base::DoNothing());
  routine_->SetObserver(observer->receiver_.BindNewPipeAndPassRemote());
  routine_->Start();
  observer->receiver_.FlushForTesting();
  EXPECT_EQ(observer->state_->percentage, 0);
  EXPECT_TRUE(observer->state_->state_union->is_running());

  // Fast forward for observer to update percentage.
  task_environment_.FastForwardBy(base::Seconds(30));
  observer->receiver_.FlushForTesting();
  EXPECT_EQ(observer->state_->percentage, 50);
  EXPECT_TRUE(observer->state_->state_union->is_running());

  task_environment_.FastForwardBy(base::Seconds(30));
  FinishUrandomDelegate(true);
  observer->receiver_.FlushForTesting();
  EXPECT_EQ(observer->state_->percentage, 100);
  EXPECT_TRUE(observer->state_->state_union->is_finished());
}

}  // namespace
}  // namespace diagnostics
