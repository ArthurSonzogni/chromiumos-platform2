// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/memory_and_cpu/prime_search.h"

#include <memory>
#include <string>
#include <utility>

#include <base/functional/callback_helpers.h>
#include <base/test/bind.h>
#include <base/test/task_environment.h>
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

class PrimeSearchRoutineTestBase : public testing::Test {
 protected:
  PrimeSearchRoutineTestBase() = default;
  PrimeSearchRoutineTestBase(const PrimeSearchRoutineTestBase&) = delete;
  PrimeSearchRoutineTestBase& operator=(const PrimeSearchRoutineTestBase&) =
      delete;

  void SetUp() override {
    EXPECT_CALL(*mock_context_.mock_executor(), RunPrimeSearch(_, _, _, _))
        .WillRepeatedly(
            [=, this](
                base::TimeDelta exec_duration, uint64_t max_num,
                mojo::PendingReceiver<ash::cros_healthd::mojom::ProcessControl>
                    receiver,
                Executor::RunPrimeSearchCallback callback) {
              fake_process_control_.BindReceiver(std::move(receiver));
              received_exec_duration_ = exec_duration;
              received_max_num_ = max_num;
              received_callback_ = std::move(callback);
            });
  }

  // Sets the mock executor response by running the callback and returning
  // passed or failed based on input.
  void FinishPrimeSearchDelegate(bool passed) {
    std::move(received_callback_).Run(passed);
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  MockContext mock_context_;
  FakeProcessControl fake_process_control_;
  base::TimeDelta received_exec_duration_ = base::Seconds(0);
  uint64_t received_max_num_ = 0;
  Executor::RunPrimeSearchCallback received_callback_;
};

class PrimeSearchRoutineTest : public PrimeSearchRoutineTestBase {
 protected:
  PrimeSearchRoutineTest() = default;
  PrimeSearchRoutineTest(const PrimeSearchRoutineTest&) = delete;
  PrimeSearchRoutineTest& operator=(const PrimeSearchRoutineTest&) = delete;

  void SetUp() {
    PrimeSearchRoutineTestBase::SetUp();
    routine_ = std::make_unique<PrimeSearchRoutine>(
        &mock_context_, mojom::PrimeSearchRoutineArgument::New(
                            /*length_seconds=*/std::nullopt));
  }

  mojom::RoutineStatePtr RunRoutineAndWaitForExit(bool passed) {
    base::RunLoop run_loop;
    routine_->SetOnExceptionCallback(UnexpectedRoutineExceptionCallback());
    auto observer =
        std::make_unique<RoutineObserverForTesting>(run_loop.QuitClosure());
    routine_->SetObserver(observer->receiver_.BindNewPipeAndPassRemote());
    routine_->Start();
    FinishPrimeSearchDelegate(passed);
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

  std::unique_ptr<PrimeSearchRoutine> routine_;
};

class PrimeSearchRoutineAdapterTest : public PrimeSearchRoutineTestBase {
 protected:
  PrimeSearchRoutineAdapterTest() = default;
  PrimeSearchRoutineAdapterTest(const PrimeSearchRoutineAdapterTest&) = delete;
  PrimeSearchRoutineAdapterTest& operator=(
      const PrimeSearchRoutineAdapterTest&) = delete;

  void SetUp() {
    PrimeSearchRoutineTestBase::SetUp();
    routine_adapter_ = std::make_unique<RoutineAdapter>(
        mojom::RoutineArgument::Tag::kPrimeSearch);
    routine_adapter_->SetupAdapter(
        mojom::RoutineArgument::NewPrimeSearch(
            mojom::PrimeSearchRoutineArgument::New(std::nullopt)),
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
TEST_F(PrimeSearchRoutineTest, RoutineSuccess) {
  mojom::RoutineStatePtr result = RunRoutineAndWaitForExit(true);
  EXPECT_EQ(result->percentage, 100);
  EXPECT_TRUE(result->state_union->is_finished());
  EXPECT_TRUE(result->state_union->get_finished()->has_passed);
}

// Test that the routine can run successfully through adapter.
TEST_F(PrimeSearchRoutineAdapterTest, RoutineSuccess) {
  mojom::RoutineUpdatePtr update = mojom::RoutineUpdate::New();

  routine_adapter_->Start();
  FlushAdapter();
  FinishPrimeSearchDelegate(true);
  FlushAdapter();

  update = GetUpdate();
  EXPECT_EQ(update->progress_percent, 100);
  EXPECT_TRUE(update->routine_update_union->is_noninteractive_update());
  EXPECT_EQ(update->routine_update_union->get_noninteractive_update()->status,
            mojom::DiagnosticRoutineStatusEnum::kPassed);
}

// Test that the routine can fail successfully.
TEST_F(PrimeSearchRoutineTest, RoutineFailure) {
  mojom::RoutineStatePtr result = RunRoutineAndWaitForExit(false);
  EXPECT_EQ(result->percentage, 100);
  EXPECT_TRUE(result->state_union->is_finished());
  EXPECT_FALSE(result->state_union->get_finished()->has_passed);
}

// Test that the routine can fail successfully through adapter.
TEST_F(PrimeSearchRoutineAdapterTest, RoutineFailure) {
  mojom::RoutineUpdatePtr update = mojom::RoutineUpdate::New();

  routine_adapter_->Start();
  FlushAdapter();
  FinishPrimeSearchDelegate(false);
  FlushAdapter();

  update = GetUpdate();
  EXPECT_EQ(update->progress_percent, 100);
  EXPECT_TRUE(update->routine_update_union->is_noninteractive_update());
  EXPECT_EQ(update->routine_update_union->get_noninteractive_update()->status,
            mojom::DiagnosticRoutineStatusEnum::kFailed);
}

// Test that the routine defaults to 60 seconds if no duration isprovided.
TEST_F(PrimeSearchRoutineTest, DefaultTestSeconds) {
  RunRoutineAndWaitForExit(true);
  EXPECT_EQ(received_exec_duration_, base::Seconds(60));
}

// Test that the routine can run with custom time.
TEST_F(PrimeSearchRoutineTest, CustomTestSeconds) {
  routine_ = std::make_unique<PrimeSearchRoutine>(
      &mock_context_, mojom::PrimeSearchRoutineArgument::New(
                          /*exec_duration=*/base::Seconds(20)));
  RunRoutineAndWaitForExit(true);
  EXPECT_EQ(received_exec_duration_, base::Seconds(20));
}

// Test that the routine defaults to minimum running time (1 second) if invalid
// duration is provided.
TEST_F(PrimeSearchRoutineTest, InvalidTestSecondsFallbackToMinimumDefault) {
  routine_ = std::make_unique<PrimeSearchRoutine>(
      &mock_context_, mojom::PrimeSearchRoutineArgument::New(
                          /*exec_duration=*/base::Seconds(0)));
  RunRoutineAndWaitForExit(true);
  EXPECT_EQ(received_exec_duration_, base::Seconds(1));
}

// Test that the routine have the correct default search paramater.
TEST_F(PrimeSearchRoutineTest, DefaultPrimeSearchParameter) {
  routine_ = std::make_unique<PrimeSearchRoutine>(
      &mock_context_, mojom::PrimeSearchRoutineArgument::New(
                          /*exec_duration=*/base::Seconds(60)));
  RunRoutineAndWaitForExit(true);
  EXPECT_EQ(received_max_num_, 1000000);
}

// Test that the routine can customize search paramater.
TEST_F(PrimeSearchRoutineTest, CustomizePrimeSearchParameter) {
  mock_context_.fake_cros_config()->SetString(
      "/cros-healthd/routines/prime-search", "max-num", "1000");
  routine_ = std::make_unique<PrimeSearchRoutine>(
      &mock_context_, mojom::PrimeSearchRoutineArgument::New(
                          /*exec_duration=*/base::Seconds(60)));
  RunRoutineAndWaitForExit(true);
  EXPECT_EQ(received_max_num_, 1000);
}

// Test that the routine can default to minimum for invalid search paramater.
TEST_F(PrimeSearchRoutineTest, InvalidPrimeSearchParameter) {
  mock_context_.fake_cros_config()->SetString(
      "/cros-healthd/routines/prime-search", "max-num", "0");
  routine_ = std::make_unique<PrimeSearchRoutine>(
      &mock_context_, mojom::PrimeSearchRoutineArgument::New(
                          /*exec_duration=*/base::Seconds(60)));
  RunRoutineAndWaitForExit(true);
  EXPECT_EQ(received_max_num_, 2);
}

// Test that the routine can report progress correctly.
TEST_F(PrimeSearchRoutineTest, IncrementalProgress) {
  routine_ = std::make_unique<PrimeSearchRoutine>(
      &mock_context_, mojom::PrimeSearchRoutineArgument::New(
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
  FinishPrimeSearchDelegate(true);
  observer->receiver_.FlushForTesting();
  EXPECT_EQ(observer->state_->percentage, 100);
  EXPECT_TRUE(observer->state_->state_union->is_finished());
}

// Test that the routine can report progress correctly through
// adapter.
TEST_F(PrimeSearchRoutineAdapterTest, IncrementalProgress) {
  mojom::RoutineUpdatePtr update = mojom::RoutineUpdate::New();
  routine_adapter_ = std::make_unique<RoutineAdapter>(
      mojom::RoutineArgument::Tag::kPrimeSearch);
  routine_adapter_->SetupAdapter(mojom::RoutineArgument::NewPrimeSearch(
                                     mojom::PrimeSearchRoutineArgument::New(
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
  FinishPrimeSearchDelegate(true);
  FlushAdapter();
  update = GetUpdate();
  EXPECT_EQ(update->progress_percent, 100);
  EXPECT_TRUE(update->routine_update_union->is_noninteractive_update());
  EXPECT_EQ(update->routine_update_union->get_noninteractive_update()->status,
            mojom::DiagnosticRoutineStatusEnum::kPassed);
}

}  // namespace
}  // namespace diagnostics
