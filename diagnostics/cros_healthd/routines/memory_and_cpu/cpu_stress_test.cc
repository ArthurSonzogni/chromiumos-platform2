// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/memory_and_cpu/cpu_stress.h"

#include <memory>
#include <string>
#include <utility>

#include <base/functional/callback_helpers.h>
#include <base/test/bind.h>
#include <base/test/task_environment.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "diagnostics/base/file_test_utils.h"
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
using ::testing::WithArgs;

class CpuStressRoutineTestBase : public BaseFileTest {
 protected:
  CpuStressRoutineTestBase() = default;
  CpuStressRoutineTestBase(const CpuStressRoutineTestBase&) = delete;
  CpuStressRoutineTestBase& operator=(const CpuStressRoutineTestBase&) = delete;

  void SetUp() override {
    SetTestRoot(mock_context_.root_dir());
    SetMockMemoryInfo(
        "MemTotal:        3906320 kB\n"
        "MemFree:         2873180 kB\n"
        "MemAvailable:    2878980 kB\n");
    SetExecutorResponse();
  }

  void SetMockMemoryInfo(const std::string& info) {
    SetFile({"proc", "meminfo"}, info);
  }

  // Sets the mock executor response by binding receiver and storing how much
  // memory is being tested.
  void SetExecutorResponse() {
    EXPECT_CALL(*mock_context_.mock_executor(),
                RunStressAppTest(_, _, mojom::StressAppTestType::kCpuStress, _))
        .WillRepeatedly(WithArgs<1, 3>(
            [=](uint32_t test_seconds,
                mojo::PendingReceiver<ash::cros_healthd::mojom::ProcessControl>
                    receiver) {
              fake_process_control_.BindReceiver(std::move(receiver));
              received_test_seconds_ = test_seconds;
            }));
  }

  void SetExecutorReturnCode(int return_code) {
    fake_process_control_.SetReturnCode(return_code);
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  MockContext mock_context_;
  FakeProcessControl fake_process_control_;
  int received_test_seconds_ = -1;
};

class CpuStressRoutineTest : public CpuStressRoutineTestBase {
 protected:
  CpuStressRoutineTest() = default;
  CpuStressRoutineTest(const CpuStressRoutineTest&) = delete;
  CpuStressRoutineTest& operator=(const CpuStressRoutineTest&) = delete;

  void SetUp() {
    CpuStressRoutineTestBase::SetUp();
    routine_ = std::make_unique<CpuStressRoutine>(
        &mock_context_,
        mojom::CpuStressRoutineArgument::New(/*length_seconds=*/std::nullopt));
  }

  mojom::RoutineStatePtr RunRoutineAndWaitForExit() {
    base::RunLoop run_loop;
    routine_->SetOnExceptionCallback(UnexpectedRoutineExceptionCallback());
    RoutineObserverForTesting observer{run_loop.QuitClosure()};
    routine_->SetObserver(observer.receiver_.BindNewPipeAndPassRemote());
    routine_->Start();
    run_loop.Run();
    return std::move(observer.state_);
  }

  void RunRoutineAndWaitForException() {
    base::RunLoop run_loop;
    routine_->SetOnExceptionCallback(
        base::IgnoreArgs<uint32_t, const std::string&>(run_loop.QuitClosure()));
    routine_->Start();
    run_loop.Run();
  }

  std::unique_ptr<CpuStressRoutine> routine_;
};

class CpuStressRoutineAdapterTest : public CpuStressRoutineTestBase {
 protected:
  CpuStressRoutineAdapterTest() = default;
  CpuStressRoutineAdapterTest(const CpuStressRoutineAdapterTest&) = delete;
  CpuStressRoutineAdapterTest& operator=(const CpuStressRoutineAdapterTest&) =
      delete;

  void SetUp() {
    CpuStressRoutineTestBase::SetUp();
    routine_adapter_ = std::make_unique<RoutineAdapter>(
        mojom::RoutineArgument::Tag::kCpuStress);
    routine_adapter_->SetupAdapter(
        mojom::RoutineArgument::NewCpuStress(
            mojom::CpuStressRoutineArgument::New(std::nullopt)),
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

// Test that the CPU stress routine can run successfully.
TEST_F(CpuStressRoutineTest, RoutineSuccess) {
  SetExecutorReturnCode(EXIT_SUCCESS);

  mojom::RoutineStatePtr result = RunRoutineAndWaitForExit();
  EXPECT_EQ(result->percentage, 100);
  EXPECT_TRUE(result->state_union->is_finished());
  EXPECT_TRUE(result->state_union->get_finished()->has_passed);
}

// Test that we can run a routine successfully.
TEST_F(CpuStressRoutineAdapterTest, RoutineSuccess) {
  mojom::RoutineUpdatePtr update = mojom::RoutineUpdate::New();
  SetExecutorReturnCode(EXIT_SUCCESS);

  routine_adapter_->Start();
  FlushAdapter();
  update = GetUpdate();
  EXPECT_EQ(update->progress_percent, 100);
  EXPECT_TRUE(update->routine_update_union->is_noninteractive_update());
  EXPECT_EQ(update->routine_update_union->get_noninteractive_update()->status,
            mojom::DiagnosticRoutineStatusEnum::kPassed);
}

// Test that the CPU stress routine handles the parsing error.
TEST_F(CpuStressRoutineTest, RoutineParseError) {
  SetMockMemoryInfo("Incorrectly formatted meminfo contents.\n");
  RunRoutineAndWaitForException();
}

// Test that the CPU stress routine handles the parsing error.
TEST_F(CpuStressRoutineAdapterTest, RoutineParseError) {
  mojom::RoutineUpdatePtr update = mojom::RoutineUpdate::New();
  SetMockMemoryInfo("Incorrectly formatted meminfo contents.\n");

  routine_adapter_->Start();
  FlushAdapter();
  update = GetUpdate();
  EXPECT_TRUE(update->routine_update_union->is_noninteractive_update());
  EXPECT_EQ(update->routine_update_union->get_noninteractive_update()->status,
            mojom::DiagnosticRoutineStatusEnum::kError);
}

// Test that the CPU stress routine handles when there is less than 628MB memory
TEST_F(CpuStressRoutineTest, RoutineNotEnoughMemory) {
  // MemAvailable less than 628 MB.
  SetMockMemoryInfo(
      "MemTotal:        3906320 kB\n"
      "MemFree:         2873180 kB\n"
      "MemAvailable:    500000 kB\n");
  RunRoutineAndWaitForException();
}

// Test that the CPU Stress routine handles when there is less than 628MB memory
TEST_F(CpuStressRoutineAdapterTest, RoutineNotEnoughMemory) {
  mojom::RoutineUpdatePtr update = mojom::RoutineUpdate::New();
  // MemAvailable less than 628 MB.
  SetMockMemoryInfo(
      "MemTotal:        3906320 kB\n"
      "MemFree:         2873180 kB\n"
      "MemAvailable:    500000 kB\n");

  routine_adapter_->Start();
  FlushAdapter();
  update = GetUpdate();
  EXPECT_TRUE(update->routine_update_union->is_noninteractive_update());
  EXPECT_EQ(update->routine_update_union->get_noninteractive_update()->status,
            mojom::DiagnosticRoutineStatusEnum::kError);
}

TEST_F(CpuStressRoutineTest, DefaultTestSeconds) {
  SetExecutorReturnCode(EXIT_SUCCESS);
  RunRoutineAndWaitForExit();
  EXPECT_EQ(received_test_seconds_, 60);
}

TEST_F(CpuStressRoutineTest, CustomTestSeconds) {
  SetExecutorReturnCode(EXIT_SUCCESS);
  routine_ = std::make_unique<CpuStressRoutine>(
      &mock_context_, mojom::CpuStressRoutineArgument::New(
                          /*exec_duration=*/base::Seconds(20)));
  RunRoutineAndWaitForExit();
  EXPECT_EQ(received_test_seconds_, 20);
}

TEST_F(CpuStressRoutineTest, InvalidTestSecondsFallbackToDefaultOfOneMinute) {
  SetExecutorReturnCode(EXIT_SUCCESS);
  routine_ = std::make_unique<CpuStressRoutine>(
      &mock_context_,
      mojom::CpuStressRoutineArgument::New(/*exec_duration=*/base::Seconds(0)));
  RunRoutineAndWaitForExit();
  EXPECT_EQ(received_test_seconds_, 60);
}

TEST_F(CpuStressRoutineTest, IncrementalProgress) {
  routine_ = std::make_unique<CpuStressRoutine>(
      &mock_context_, mojom::CpuStressRoutineArgument::New(
                          /*exec_duration=*/base::Seconds(60)));
  routine_->SetOnExceptionCallback(UnexpectedRoutineExceptionCallback());
  RoutineObserverForTesting observer{base::DoNothing()};
  routine_->SetObserver(observer.receiver_.BindNewPipeAndPassRemote());
  routine_->Start();
  observer.receiver_.FlushForTesting();
  EXPECT_EQ(observer.state_->percentage, 0);
  EXPECT_TRUE(observer.state_->state_union->is_running());

  // Fast forward for observer to update percentage.
  task_environment_.FastForwardBy(base::Seconds(30));
  observer.receiver_.FlushForTesting();
  EXPECT_EQ(observer.state_->percentage, 50);
  EXPECT_TRUE(observer.state_->state_union->is_running());

  task_environment_.FastForwardBy(base::Seconds(30));
  SetExecutorReturnCode(EXIT_SUCCESS);
  fake_process_control_.receiver().FlushForTesting();
  observer.receiver_.FlushForTesting();
  EXPECT_EQ(observer.state_->percentage, 100);
  EXPECT_TRUE(observer.state_->state_union->is_finished());
}

TEST_F(CpuStressRoutineAdapterTest, IncrementalProgress) {
  mojom::RoutineUpdatePtr update = mojom::RoutineUpdate::New();
  routine_adapter_ =
      std::make_unique<RoutineAdapter>(mojom::RoutineArgument::Tag::kCpuStress);
  routine_adapter_->SetupAdapter(
      mojom::RoutineArgument::NewCpuStress(mojom::CpuStressRoutineArgument::New(
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
  SetExecutorReturnCode(EXIT_SUCCESS);
  FlushAdapter();
  update = GetUpdate();
  EXPECT_EQ(update->progress_percent, 100);
  EXPECT_TRUE(update->routine_update_union->is_noninteractive_update());
  EXPECT_EQ(update->routine_update_union->get_noninteractive_update()->status,
            mojom::DiagnosticRoutineStatusEnum::kPassed);
}

// Test that the CPU stress routine will raise error if the executor
// disconnects.
TEST_F(CpuStressRoutineTest, ExecutorDisconnectBeforeFinishedError) {
  base::RunLoop run_loop;
  routine_->SetOnExceptionCallback(
      base::IgnoreArgs<uint32_t, const std::string&>(run_loop.QuitClosure()));
  routine_->Start();
  fake_process_control_.receiver().reset();
  run_loop.Run();
}

// Test that the CPU stress routine will raise error if the executor
// disconnects.
TEST_F(CpuStressRoutineAdapterTest, ExecutorDisconnectBeforeFinishedError) {
  mojom::RoutineUpdatePtr update = mojom::RoutineUpdate::New();
  routine_adapter_->Start();
  FlushAdapter();
  fake_process_control_.receiver().reset();
  routine_adapter_->FlushRoutineControlForTesting();
  update = GetUpdate();
  EXPECT_TRUE(update->routine_update_union->is_noninteractive_update());
  EXPECT_EQ(update->routine_update_union->get_noninteractive_update()->status,
            mojom::DiagnosticRoutineStatusEnum::kError);
}

}  // namespace
}  // namespace diagnostics
