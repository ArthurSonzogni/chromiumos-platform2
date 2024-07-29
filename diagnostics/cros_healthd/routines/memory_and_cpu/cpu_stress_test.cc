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
#include <base/test/test_future.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "diagnostics/base/file_test_utils.h"
#include "diagnostics/cros_healthd/executor/utils/fake_process_control.h"
#include "diagnostics/cros_healthd/routines/routine_adapter.h"
#include "diagnostics/cros_healthd/routines/routine_observer_for_testing.h"
#include "diagnostics/cros_healthd/routines/routine_service.h"
#include "diagnostics/cros_healthd/routines/routine_v2_test_utils.h"
#include "diagnostics/cros_healthd/system/fake_meminfo_reader.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "diagnostics/mojom/public/cros_healthd_diagnostics.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_routines.mojom.h"

namespace diagnostics {
namespace {

namespace mojom = ash::cros_healthd::mojom;

using ::testing::_;
using ::testing::WithArgs;

class CpuStressRoutineTestBase : public BaseFileTest {
 public:
  CpuStressRoutineTestBase(const CpuStressRoutineTestBase&) = delete;
  CpuStressRoutineTestBase& operator=(const CpuStressRoutineTestBase&) = delete;

 protected:
  CpuStressRoutineTestBase() = default;

  void SetUp() override {
    // MemAvailable more than 628 MiB.
    mock_context_.fake_meminfo_reader()->SetError(false);
    mock_context_.fake_meminfo_reader()->SetAvailableMemoryKib(2878980);

    SetExecutorResponse();
  }

  // Sets the mock executor response by binding receiver and storing how much
  // memory is being tested.
  void SetExecutorResponse() {
    EXPECT_CALL(*mock_context_.mock_executor(),
                RunStressAppTest(_, _, mojom::StressAppTestType::kCpuStress, _))
        .WillRepeatedly(WithArgs<1, 3>(
            [=, this](uint32_t test_seconds,
                      mojo::PendingReceiver<mojom::ProcessControl> receiver) {
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
 public:
  CpuStressRoutineTest(const CpuStressRoutineTest&) = delete;
  CpuStressRoutineTest& operator=(const CpuStressRoutineTest&) = delete;

 protected:
  CpuStressRoutineTest() = default;

  void SetUp() {
    CpuStressRoutineTestBase::SetUp();
    routine_ = std::make_unique<CpuStressRoutine>(
        &mock_context_,
        mojom::CpuStressRoutineArgument::New(/*length_seconds=*/std::nullopt));
  }

  mojom::RoutineStatePtr RunRoutineAndWaitForExit() {
    routine_->SetOnExceptionCallback(UnexpectedRoutineExceptionCallback());
    RoutineObserverForTesting observer;
    routine_->SetObserver(observer.receiver_.BindNewPipeAndPassRemote());
    routine_->Start();
    observer.WaitUntilRoutineFinished();
    return std::move(observer.state_);
  }

  void RunRoutineAndWaitForException() {
    base::test::TestFuture<uint32_t, const std::string&> future;
    routine_->SetOnExceptionCallback(future.GetCallback());
    routine_->Start();
    EXPECT_TRUE(future.Wait());
  }

  std::unique_ptr<CpuStressRoutine> routine_;
};

class CpuStressRoutineAdapterTest : public CpuStressRoutineTestBase {
 public:
  CpuStressRoutineAdapterTest(const CpuStressRoutineAdapterTest&) = delete;
  CpuStressRoutineAdapterTest& operator=(const CpuStressRoutineAdapterTest&) =
      delete;

 protected:
  CpuStressRoutineAdapterTest() = default;

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
    routine_adapter_->PopulateStatusUpdate(/*include_output=*/true, *update);
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
  mock_context_.fake_meminfo_reader()->SetError(true);
  RunRoutineAndWaitForException();
}

// Test that the CPU stress routine handles the parsing error.
TEST_F(CpuStressRoutineAdapterTest, RoutineParseError) {
  mojom::RoutineUpdatePtr update = mojom::RoutineUpdate::New();
  mock_context_.fake_meminfo_reader()->SetError(true);

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
  mock_context_.fake_meminfo_reader()->SetError(false);
  mock_context_.fake_meminfo_reader()->SetAvailableMemoryKib(500000);
  RunRoutineAndWaitForException();
}

// Test that the CPU Stress routine handles when there is less than 628MB memory
TEST_F(CpuStressRoutineAdapterTest, RoutineNotEnoughMemory) {
  mojom::RoutineUpdatePtr update = mojom::RoutineUpdate::New();
  // MemAvailable less than 628 MB.
  mock_context_.fake_meminfo_reader()->SetError(false);
  mock_context_.fake_meminfo_reader()->SetAvailableMemoryKib(500000);

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
  RoutineObserverForTesting observer;
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
  base::test::TestFuture<uint32_t, const std::string&> future;
  routine_->SetOnExceptionCallback(future.GetCallback());
  routine_->Start();
  fake_process_control_.receiver().reset();
  EXPECT_TRUE(future.Wait());
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
