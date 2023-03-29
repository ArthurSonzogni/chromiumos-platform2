// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/memory_and_cpu/cpu_stress_v2.h"

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
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "diagnostics/mojom/public/cros_healthd_diagnostics.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_routines.mojom.h"

namespace diagnostics {
namespace {

namespace mojom = ash::cros_healthd::mojom;

using ::testing::_;
using ::testing::Invoke;
using ::testing::WithArgs;

class CpuStressRoutineV2TestBase : public BaseFileTest {
 protected:
  CpuStressRoutineV2TestBase() = default;
  CpuStressRoutineV2TestBase(const CpuStressRoutineV2TestBase&) = delete;
  CpuStressRoutineV2TestBase& operator=(const CpuStressRoutineV2TestBase&) =
      delete;

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
        .WillRepeatedly(WithArgs<1, 3>(Invoke(
            [=](uint32_t test_seconds,
                mojo::PendingReceiver<ash::cros_healthd::mojom::ProcessControl>
                    receiver) mutable {
              fake_process_control_.BindReceiver(std::move(receiver));
              received_test_seconds_ = test_seconds;
            })));
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

class CpuStressRoutineV2Test : public CpuStressRoutineV2TestBase {
 protected:
  CpuStressRoutineV2Test() = default;
  CpuStressRoutineV2Test(const CpuStressRoutineV2Test&) = delete;
  CpuStressRoutineV2Test& operator=(const CpuStressRoutineV2Test&) = delete;

  void SetUp() {
    CpuStressRoutineV2TestBase::SetUp();
    routine_ = std::make_unique<CpuStressRoutineV2>(
        &mock_context_,
        mojom::CpuStressRoutineArgument::New(/*length_seconds=*/std::nullopt));
  }

  mojom::RoutineStatePtr RunRoutineAndWaitForExit() {
    base::RunLoop run_loop;
    routine_->SetOnExceptionCallback(
        base::BindOnce([](uint32_t error, const std::string& reason) {
          EXPECT_TRUE(false)
              << "An exception has occurred when it shouldn't have.";
        }));
    auto observer =
        std::make_unique<RoutineObserverForTesting>(run_loop.QuitClosure());
    routine_->AddObserver(observer->receiver_.BindNewPipeAndPassRemote());
    routine_->Start();
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

  std::unique_ptr<CpuStressRoutineV2> routine_;
};

// Test that the CPU stress routine can run successfully.
TEST_F(CpuStressRoutineV2Test, RoutineSuccess) {
  SetExecutorReturnCode(EXIT_SUCCESS);

  mojom::RoutineStatePtr result = RunRoutineAndWaitForExit();
  EXPECT_EQ(result->percentage, 100);
  EXPECT_TRUE(result->state_union->is_finished());
  EXPECT_TRUE(result->state_union->get_finished()->has_passed);
}

// Test that the CPU stress routine handles the parsing error.
TEST_F(CpuStressRoutineV2Test, RoutineParseError) {
  SetMockMemoryInfo("Incorrectly formatted meminfo contents.\n");
  RunRoutineAndWaitForException();
}

// Test that the CPU stress routine handles when there is less than 628MB memory
TEST_F(CpuStressRoutineV2Test, RoutineNotEnoughMemory) {
  // MemAvailable less than 628 MB.
  SetMockMemoryInfo(
      "MemTotal:        3906320 kB\n"
      "MemFree:         2873180 kB\n"
      "MemAvailable:    500000 kB\n");
  RunRoutineAndWaitForException();
}

TEST_F(CpuStressRoutineV2Test, DefaultTestSeconds) {
  SetExecutorReturnCode(EXIT_SUCCESS);
  RunRoutineAndWaitForExit();
  EXPECT_EQ(received_test_seconds_, 60);
}

TEST_F(CpuStressRoutineV2Test, CustomTestSeconds) {
  SetExecutorReturnCode(EXIT_SUCCESS);
  routine_ = std::make_unique<CpuStressRoutineV2>(
      &mock_context_, mojom::CpuStressRoutineArgument::New(
                          /*exec_duration=*/base::Seconds(20)));
  RunRoutineAndWaitForExit();
  EXPECT_EQ(received_test_seconds_, 20);
}

TEST_F(CpuStressRoutineV2Test, InvalidTestSecondsFallbackToDefaultOfOneMinute) {
  SetExecutorReturnCode(EXIT_SUCCESS);
  routine_ = std::make_unique<CpuStressRoutineV2>(
      &mock_context_,
      mojom::CpuStressRoutineArgument::New(/*exec_duration=*/base::Seconds(0)));
  RunRoutineAndWaitForExit();
  EXPECT_EQ(received_test_seconds_, 60);
}

TEST_F(CpuStressRoutineV2Test, IncrementalProgress) {
  routine_ = std::make_unique<CpuStressRoutineV2>(
      &mock_context_, mojom::CpuStressRoutineArgument::New(
                          /*exec_duration=*/base::Seconds(60)));
  routine_->SetOnExceptionCallback(
      base::BindOnce(
          [](uint32_t error, const std::string& reason) {
            EXPECT_TRUE(false)
                << "An exception has occurred when it shouldn't have.";
          }));
  auto observer =
      std::make_unique<RoutineObserverForTesting>(base::DoNothing());
  routine_->AddObserver(observer->receiver_.BindNewPipeAndPassRemote());
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
  SetExecutorReturnCode(EXIT_SUCCESS);
  fake_process_control_.receiver().FlushForTesting();
  observer->receiver_.FlushForTesting();
  EXPECT_EQ(observer->state_->percentage, 100);
  EXPECT_TRUE(observer->state_->state_union->is_finished());
}

// Test that the CPU stress routine will raise error if the executor
// disconnects.
TEST_F(CpuStressRoutineV2Test, ExecutorDisconnectBeforeFinishedError) {
  base::RunLoop run_loop;
  routine_->SetOnExceptionCallback(
      base::IgnoreArgs<uint32_t, const std::string&>(run_loop.QuitClosure()));
  routine_->Start();
  fake_process_control_.receiver().reset();
  run_loop.Run();
}

}  // namespace
}  // namespace diagnostics
