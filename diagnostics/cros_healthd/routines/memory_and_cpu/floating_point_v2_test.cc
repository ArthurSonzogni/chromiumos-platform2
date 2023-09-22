// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/memory_and_cpu/floating_point_v2.h"

#include <memory>
#include <string>
#include <utility>

#include <base/functional/callback_helpers.h>
#include <base/test/bind.h>
#include <base/test/task_environment.h>
#include <base/time/time.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "diagnostics/base/file_test_utils.h"
#include "diagnostics/cros_healthd/executor/executor.h"
#include "diagnostics/cros_healthd/executor/utils/fake_process_control.h"
#include "diagnostics/cros_healthd/routine_adapter.h"
#include "diagnostics/cros_healthd/routines/routine_observer_for_testing.h"
#include "diagnostics/cros_healthd/routines/routine_service.h"
#include "diagnostics/cros_healthd/routines/routine_v2_test_utils.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "diagnostics/mojom/public/cros_healthd_diagnostics.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_routines.mojom.h"

namespace diagnostics {
namespace {

namespace mojom = ash::cros_healthd::mojom;

using ::testing::_;

class FloatingPointRoutineV2TestBase : public testing::Test {
 protected:
  FloatingPointRoutineV2TestBase() = default;
  FloatingPointRoutineV2TestBase(const FloatingPointRoutineV2TestBase&) =
      delete;
  FloatingPointRoutineV2TestBase& operator=(
      const FloatingPointRoutineV2TestBase&) = delete;

  void SetUp() override {
    EXPECT_CALL(*mock_context_.mock_executor(), RunFloatingPoint(_, _, _))
        .WillRepeatedly(
            [=](base::TimeDelta exec_duration,
                mojo::PendingReceiver<ash::cros_healthd::mojom::ProcessControl>
                    receiver,
                Executor::RunFloatingPointCallback callback) {
              fake_process_control_.BindReceiver(std::move(receiver));
              received_exec_duration_ = exec_duration;
              received_callback_ = std::move(callback);
            });
  }

  // Sets the mock executor response by running the callback and returning
  // passed or failed based on input.
  void FinishFloatingPointDelegate(bool passed) {
    std::move(received_callback_).Run(passed);
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  MockContext mock_context_;
  FakeProcessControl fake_process_control_;
  base::TimeDelta received_exec_duration_ = base::Seconds(0);
  Executor::RunFloatingPointCallback received_callback_;
};

class FloatingPointRoutineV2Test : public FloatingPointRoutineV2TestBase {
 protected:
  FloatingPointRoutineV2Test() = default;
  FloatingPointRoutineV2Test(const FloatingPointRoutineV2Test&) = delete;
  FloatingPointRoutineV2Test& operator=(const FloatingPointRoutineV2Test&) =
      delete;

  void SetUp() {
    FloatingPointRoutineV2TestBase::SetUp();
    routine_ = std::make_unique<FloatingPointRoutineV2>(
        &mock_context_, mojom::FloatingPointRoutineArgument::New(
                            /*exec_duration=*/std::nullopt));
  }

  mojom::RoutineStatePtr RunRoutineAndWaitForExit(bool passed) {
    base::RunLoop run_loop;
    routine_->SetOnExceptionCallback(UnexpectedRoutineExceptionCallback());
    auto observer =
        std::make_unique<RoutineObserverForTesting>(run_loop.QuitClosure());
    routine_->SetObserver(observer->receiver_.BindNewPipeAndPassRemote());
    routine_->Start();
    FinishFloatingPointDelegate(passed);
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

  std::unique_ptr<FloatingPointRoutineV2> routine_;
};

class FloatingPointRoutineV2AdapterTest
    : public FloatingPointRoutineV2TestBase {
 protected:
  FloatingPointRoutineV2AdapterTest() = default;
  FloatingPointRoutineV2AdapterTest(const FloatingPointRoutineV2AdapterTest&) =
      delete;
  FloatingPointRoutineV2AdapterTest& operator=(
      const FloatingPointRoutineV2AdapterTest&) = delete;

  void SetUp() {
    FloatingPointRoutineV2TestBase::SetUp();
    routine_adapter_ = std::make_unique<RoutineAdapter>(
        mojom::RoutineArgument::Tag::kFloatingPoint);
    routine_adapter_->SetupAdapter(
        mojom::RoutineArgument::NewFloatingPoint(
            mojom::FloatingPointRoutineArgument::New(std::nullopt)),
        &routine_service_);
  }

  // A utility function to flush the routine control and process control.
  void FlushAdapter() {
    // Flush the routine for all request to executor through process
    // control.
    routine_adapter_->FlushRoutineControlForTesting();
    // No need to continue if there is an error and the receiver has
    // disconnected already.
    if (fake_process_control_.IsConnected()) {
      // Flush the process control to return all request to routine.
      fake_process_control_.receiver().FlushForTesting();
      // Flush the routine control once more to run any callbacks called by
      // fake_process_control.
      routine_adapter_->FlushRoutineControlForTesting();
    }
  }

  mojom::RoutineUpdatePtr GetUpdate() {
    mojom::RoutineUpdatePtr update = mojom::RoutineUpdate::New();
    routine_adapter_->PopulateStatusUpdate(update.get(), true);
    return update;
  }

  RoutineService routine_service_{&mock_context_};
  std::unique_ptr<RoutineAdapter> routine_adapter_;
};

// Test that the routine can run successfully.
TEST_F(FloatingPointRoutineV2Test, RoutineSuccess) {
  mojom::RoutineStatePtr result = RunRoutineAndWaitForExit(true);
  EXPECT_EQ(result->percentage, 100);
  EXPECT_TRUE(result->state_union->is_finished());
  EXPECT_TRUE(result->state_union->get_finished()->has_passed);
}

// Test that the routine can run successfully through adapter.
TEST_F(FloatingPointRoutineV2AdapterTest, RoutineSuccess) {
  mojom::RoutineUpdatePtr update = mojom::RoutineUpdate::New();

  routine_adapter_->Start();
  FlushAdapter();
  FinishFloatingPointDelegate(true);
  FlushAdapter();

  update = GetUpdate();
  EXPECT_EQ(update->progress_percent, 100);
  EXPECT_TRUE(update->routine_update_union->is_noninteractive_update());
  EXPECT_EQ(update->routine_update_union->get_noninteractive_update()->status,
            mojom::DiagnosticRoutineStatusEnum::kPassed);
}

// Test that the routine can fail successfully.
TEST_F(FloatingPointRoutineV2Test, RoutineFailure) {
  mojom::RoutineStatePtr result = RunRoutineAndWaitForExit(false);
  EXPECT_EQ(result->percentage, 100);
  EXPECT_TRUE(result->state_union->is_finished());
  EXPECT_FALSE(result->state_union->get_finished()->has_passed);
}

// Test that the routine can fail successfully through adapter.
TEST_F(FloatingPointRoutineV2AdapterTest, RoutineFailure) {
  mojom::RoutineUpdatePtr update = mojom::RoutineUpdate::New();

  routine_adapter_->Start();
  FlushAdapter();
  FinishFloatingPointDelegate(false);
  FlushAdapter();

  update = GetUpdate();
  EXPECT_EQ(update->progress_percent, 100);
  EXPECT_TRUE(update->routine_update_union->is_noninteractive_update());
  EXPECT_EQ(update->routine_update_union->get_noninteractive_update()->status,
            mojom::DiagnosticRoutineStatusEnum::kFailed);
}

// Test that the routine defaults to 60 seconds if no duration is provided.
TEST_F(FloatingPointRoutineV2Test, DefaultTestSeconds) {
  RunRoutineAndWaitForExit(true);
  EXPECT_EQ(received_exec_duration_, base::Seconds(60));
}

// Test that the routine can run with custom time.
TEST_F(FloatingPointRoutineV2Test, CustomTestSeconds) {
  routine_ = std::make_unique<FloatingPointRoutineV2>(
      &mock_context_, mojom::FloatingPointRoutineArgument::New(
                          /*exec_duration=*/base::Seconds(20)));
  RunRoutineAndWaitForExit(true);
  EXPECT_EQ(received_exec_duration_, base::Seconds(20));
}

// Test that the routine defaults to minimum running time (1 second) if invalid
// duration is provided.
TEST_F(FloatingPointRoutineV2Test, InvalidTestSecondsFallbackToMinimumDefault) {
  routine_ = std::make_unique<FloatingPointRoutineV2>(
      &mock_context_, mojom::FloatingPointRoutineArgument::New(
                          /*exec_duration=*/base::Seconds(0)));
  RunRoutineAndWaitForExit(true);
  EXPECT_EQ(received_exec_duration_, base::Seconds(1));
}

// Test that the routine can report progress correctly.
TEST_F(FloatingPointRoutineV2Test, IncrementalProgress) {
  routine_ = std::make_unique<FloatingPointRoutineV2>(
      &mock_context_, mojom::FloatingPointRoutineArgument::New(
                          /*exec_duration=*/base::Seconds(60)));
  routine_->SetOnExceptionCallback(UnexpectedRoutineExceptionCallback());
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
  FinishFloatingPointDelegate(true);
  observer->receiver_.FlushForTesting();
  EXPECT_EQ(observer->state_->percentage, 100);
  EXPECT_TRUE(observer->state_->state_union->is_finished());
}

// Test that the routine can report progress correctly through
// adapter.
TEST_F(FloatingPointRoutineV2AdapterTest, IncrementalProgress) {
  mojom::RoutineUpdatePtr update = mojom::RoutineUpdate::New();
  routine_adapter_ = std::make_unique<RoutineAdapter>(
      mojom::RoutineArgument::Tag::kFloatingPoint);
  routine_adapter_->SetupAdapter(mojom::RoutineArgument::NewFloatingPoint(
                                     mojom::FloatingPointRoutineArgument::New(
                                         /*exec_duration=*/base::Seconds(60))),
                                 &routine_service_);

  routine_adapter_->Start();
  FlushAdapter();
  update = GetUpdate();
  EXPECT_EQ(update->progress_percent, 0);
  EXPECT_TRUE(update->routine_update_union->is_noninteractive_update());
  EXPECT_EQ(update->routine_update_union->get_noninteractive_update()->status,
            mojom::DiagnosticRoutineStatusEnum::kRunning);

  // Fast forward for adapter to update percentage.
  task_environment_.FastForwardBy(base::Seconds(30));
  FlushAdapter();
  update = GetUpdate();
  EXPECT_EQ(update->progress_percent, 50);
  EXPECT_TRUE(update->routine_update_union->is_noninteractive_update());
  EXPECT_EQ(update->routine_update_union->get_noninteractive_update()->status,
            mojom::DiagnosticRoutineStatusEnum::kRunning);

  // Fast forward for routine to finish running.
  task_environment_.FastForwardBy(base::Seconds(30));
  FlushAdapter();
  FinishFloatingPointDelegate(true);
  FlushAdapter();
  update = GetUpdate();
  EXPECT_EQ(update->progress_percent, 100);
  EXPECT_TRUE(update->routine_update_union->is_noninteractive_update());
  EXPECT_EQ(update->routine_update_union->get_noninteractive_update()->status,
            mojom::DiagnosticRoutineStatusEnum::kPassed);
}

}  // namespace
}  // namespace diagnostics
