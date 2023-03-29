// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <cstdlib>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/functional/bind.h>
#include <base/functional/callback_forward.h>
#include <base/functional/callback_helpers.h>
#include <base/json/json_writer.h>
#include <base/test/bind.h>
#include <base/test/task_environment.h>
#include <base/values.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <mojo/public/cpp/bindings/callback_helpers.h>
#include <mojo/public/cpp/bindings/receiver.h>
#include <mojo/public/cpp/system/handle.h>

#include "diagnostics/base/file_test_utils.h"
#include "diagnostics/base/mojo_utils.h"
#include "diagnostics/cros_healthd/executor/utils/fake_process_control.h"
#include "diagnostics/cros_healthd/mojom/executor.mojom.h"
#include "diagnostics/cros_healthd/routines/memory_and_cpu/constants.h"
#include "diagnostics/cros_healthd/routines/memory_and_cpu/memory_v2.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "diagnostics/mojom/public/cros_healthd_diagnostics.mojom.h"

namespace diagnostics {
namespace {

namespace mojom = ash::cros_healthd::mojom;
using ::testing::_;
using ::testing::Invoke;
using ::testing::WithArg;
using ::testing::WithArgs;

// Location of files containing test data (fake memtester output).
constexpr char kTestDataRoot[] =
    "cros_healthd/routines/memory_and_cpu/testdata";

#if ULONG_MAX == 4294967295UL
constexpr int kBitFlipPercentage = 57;
#elif ULONG_MAX == 18446744073709551615ULL
constexpr int kBitFlipPercentage = 42;
#endif

class RoutineObserverImpl : public mojom::RoutineObserver {
 public:
  explicit RoutineObserverImpl(base::OnceClosure on_finished)
      : receiver_{this /* impl */}, on_finished_(std::move(on_finished)) {}
  RoutineObserverImpl(const RoutineObserverImpl&) = delete;
  RoutineObserverImpl& operator=(const RoutineObserverImpl&) = delete;
  ~RoutineObserverImpl() override = default;

  void OnRoutineStateChange(mojom::RoutineStatePtr state) override {
    state_ = std::move(state);
    if (state_->state_union->is_finished()) {
      CHECK(on_finished_);
      std::move(on_finished_).Run();
    }
  }

  mojom::RoutineStatePtr state_;
  mojo::Receiver<mojom::RoutineObserver> receiver_;

 private:
  base::OnceClosure on_finished_;
};

class MemoryRoutineV2TestBase : public BaseFileTest {
 protected:
  MemoryRoutineV2TestBase() = default;
  MemoryRoutineV2TestBase(const MemoryRoutineV2TestBase&) = delete;
  MemoryRoutineV2TestBase& operator=(const MemoryRoutineV2TestBase&) = delete;

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

  std::set<mojom::MemtesterTestItemEnum> GetExpectedMemtesterTests(
      std::set<mojom::MemtesterTestItemEnum> unexpected_subtests) {
    std::set<mojom::MemtesterTestItemEnum> expected_subtests;
    for (int i = static_cast<int>(mojom::MemtesterTestItemEnum::kUnknown) + 1;
         i <= static_cast<int>(mojom::MemtesterTestItemEnum::kMaxValue); i++) {
      auto subtest = static_cast<mojom::MemtesterTestItemEnum>(i);
      if (!unexpected_subtests.count(subtest)) {
        expected_subtests.insert(subtest);
      }
    }
    return expected_subtests;
  }

  // Sets the mock executor response by binding receiver and storing how much
  // memory is being tested.
  void SetExecutorResponse() {
    EXPECT_CALL(*mock_context_.mock_executor(), RunMemtesterV2(_, _))
        .WillRepeatedly(WithArgs<0, 1>(Invoke(
            [=](uint32_t testing_mem_kib,
                mojo::PendingReceiver<ash::cros_healthd::mojom::ProcessControl>
                    receiver) mutable {
              fake_process_control_.BindReceiver(std::move(receiver));
              received_testing_mem_kib_ = testing_mem_kib;
            })));
  }

  void SetExecutorOutput(const std::string& output) {
    fake_process_control_.SetStdoutFileContent(output);
    fake_process_control_.SetStderrFileContent(output);
  }

  void SetExecutorOutputFromTestFile(const std::string& file_name) {
    std::string output;
    EXPECT_TRUE(base::ReadFileToString(
        base::FilePath(kTestDataRoot).Append(file_name), &output));
    fake_process_control_.SetStdoutFileContent(output);
    fake_process_control_.SetStderrFileContent(output);
  }

  void SetExecutorReturnCode(int return_code) {
    fake_process_control_.SetReturnCode(return_code);
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  MockContext mock_context_;
  FakeProcessControl fake_process_control_;
  int received_testing_mem_kib_ = -1;
};

class MemoryRoutineV2Test : public MemoryRoutineV2TestBase {
 protected:
  MemoryRoutineV2Test() = default;
  MemoryRoutineV2Test(const MemoryRoutineV2Test&) = delete;
  MemoryRoutineV2Test& operator=(const MemoryRoutineV2Test&) = delete;

  void SetUp() {
    MemoryRoutineV2TestBase::SetUp();
    routine_ = std::make_unique<MemoryRoutineV2>(
        &mock_context_, mojom::MemoryRoutineArgument::New(std::nullopt));
  }

  mojom::RoutineStatePtr RunRoutineAndWaitForExit() {
    base::RunLoop run_loop;
    routine_->SetOnExceptionCallback(
        base::BindOnce([](uint32_t error, const std::string& reason) {
          CHECK(false) << "An exception has occurred when it shouldn't have.";
        }));
    auto observer = std::make_unique<RoutineObserverImpl>(
        base::BindOnce(run_loop.QuitClosure()));
    routine_->AddObserver(observer->receiver_.BindNewPipeAndPassRemote());
    routine_->Start();
    run_loop.Run();
    return std::move(observer->state_);
  }

  void RunRoutineAndWaitForException() {
    base::RunLoop run_loop;
    routine_->SetOnExceptionCallback(base::BindLambdaForTesting(
        [&](uint32_t error, const std::string& reason) { run_loop.Quit(); }));
    routine_->Start();
    run_loop.Run();
  }

  std::set<mojom::MemtesterTestItemEnum> VectorToSet(
      std::vector<mojom::MemtesterTestItemEnum> v) {
    return std::set<mojom::MemtesterTestItemEnum>(v.begin(), v.end());
  }

  std::unique_ptr<MemoryRoutineV2> routine_;
};

// Test that the memory routine can run successfully.
TEST_F(MemoryRoutineV2Test, RoutineSuccess) {
  SetExecutorOutputFromTestFile("all_test_passed_output");
  SetExecutorReturnCode(EXIT_SUCCESS);

  mojom::RoutineStatePtr result = RunRoutineAndWaitForExit();
  EXPECT_EQ(result->percentage, 100);
  EXPECT_TRUE(result->state_union->is_finished());
  EXPECT_TRUE(result->state_union->get_finished()->has_passed);
  std::set<mojom::MemtesterTestItemEnum> expected_failed{};
  std::set<mojom::MemtesterTestItemEnum> expected_passed =
      GetExpectedMemtesterTests(expected_failed);
  EXPECT_EQ(VectorToSet(result->state_union->get_finished()
                            ->detail->get_memory()
                            ->result->passed_items),
            expected_passed);
  EXPECT_EQ(VectorToSet(result->state_union->get_finished()
                            ->detail->get_memory()
                            ->result->failed_items),
            expected_failed);
}

// Test that the memory routine handles the parsing error.
TEST_F(MemoryRoutineV2Test, RoutineParseError) {
  SetMockMemoryInfo("Incorrectly formatted meminfo contents.\n");
  RunRoutineAndWaitForException();
}

// Test that the memory routine handles when there is not much memory left.
TEST_F(MemoryRoutineV2Test, RoutineLessThan500MBMemory) {
  // MemAvailable less than 500 MiB.
  SetMockMemoryInfo(
      "MemTotal:        3906320 kB\n"
      "MemFree:         2873180 kB\n"
      "MemAvailable:    278980 kB\n");
  SetExecutorOutputFromTestFile("all_test_passed_output");
  SetExecutorReturnCode(EXIT_SUCCESS);

  mojom::RoutineStatePtr result = RunRoutineAndWaitForExit();
  EXPECT_EQ(result->percentage, 100);
  EXPECT_TRUE(result->state_union->is_finished());
  EXPECT_TRUE(result->state_union->get_finished()->has_passed);
  // If the available memory is too little, run with the minimum memory
  // memtester allows (4 Kib).
  EXPECT_EQ(received_testing_mem_kib_, 4);
}

// Test that the memory routine handles when there is less than 4KB memory
TEST_F(MemoryRoutineV2Test, RoutineNotEnoughMemory) {
  // MemAvailable less than 4 KB.
  SetMockMemoryInfo(
      "MemTotal:        3906320 kB\n"
      "MemFree:         2873180 kB\n"
      "MemAvailable:    3 kB\n");
  RunRoutineAndWaitForException();
}

// Test that the memory routine handles the memtester binary failing to run.
TEST_F(MemoryRoutineV2Test, MemtesterFailedToRunError) {
  SetExecutorOutput("");
  SetExecutorReturnCode(MemtesterErrorCodes::kAllocatingLockingInvokingError);
  RunRoutineAndWaitForException();
}

// Test that the memory routine handles a stuck address failure.
TEST_F(MemoryRoutineV2Test, StuckAddressFailure) {
  SetExecutorOutputFromTestFile("stuck_address_failed_output");
  SetExecutorReturnCode(MemtesterErrorCodes::kStuckAddressTestError);

  mojom::RoutineStatePtr result = RunRoutineAndWaitForExit();
  EXPECT_EQ(result->percentage, 100);
  EXPECT_TRUE(result->state_union->is_finished());
  EXPECT_FALSE(result->state_union->get_finished()->has_passed);
  std::set<mojom::MemtesterTestItemEnum> expected_failed{
      mojom::MemtesterTestItemEnum::kStuckAddress};
  std::set<mojom::MemtesterTestItemEnum> expected_passed =
      GetExpectedMemtesterTests(expected_failed);
  EXPECT_EQ(VectorToSet(result->state_union->get_finished()
                            ->detail->get_memory()
                            ->result->passed_items),
            expected_passed);
  EXPECT_EQ(VectorToSet(result->state_union->get_finished()
                            ->detail->get_memory()
                            ->result->failed_items),
            expected_failed);
}

// Test that the memory routine handles multiple test failure.
TEST_F(MemoryRoutineV2Test, MultipleTestFailure) {
  SetExecutorOutputFromTestFile("stuck_address_and_bit_flip_failed_output");
  SetExecutorReturnCode(MemtesterErrorCodes::kStuckAddressTestError |
                        MemtesterErrorCodes::kOtherTestError);

  mojom::RoutineStatePtr result = RunRoutineAndWaitForExit();
  EXPECT_EQ(result->percentage, 100);
  EXPECT_TRUE(result->state_union->is_finished());
  EXPECT_FALSE(result->state_union->get_finished()->has_passed);
  std::set<mojom::MemtesterTestItemEnum> expected_failed{
      mojom::MemtesterTestItemEnum::kStuckAddress,
      mojom::MemtesterTestItemEnum::kBitFlip};
  std::set<mojom::MemtesterTestItemEnum> expected_passed =
      GetExpectedMemtesterTests(expected_failed);
  EXPECT_EQ(VectorToSet(result->state_union->get_finished()
                            ->detail->get_memory()
                            ->result->passed_items),
            expected_passed);
  EXPECT_EQ(VectorToSet(result->state_union->get_finished()
                            ->detail->get_memory()
                            ->result->failed_items),
            expected_failed);
}

// Test that the memory routine handles setting a max_testing_mem_kib value.
TEST_F(MemoryRoutineV2Test, SettingMaxTestingMemKibValue) {
  SetExecutorOutputFromTestFile("all_test_passed_output");
  SetExecutorReturnCode(EXIT_SUCCESS);

  routine_ = std::make_unique<MemoryRoutineV2>(
      &mock_context_, mojom::MemoryRoutineArgument::New(1000));
  RunRoutineAndWaitForExit();
  EXPECT_EQ(received_testing_mem_kib_, 1000);
}

// Test that the memory routine is able to detect incremental progress.
TEST_F(MemoryRoutineV2Test, IncrementalProgress) {
  std::string progress_0_output, progress_bit_flip_output,
      all_test_passed_output;
  EXPECT_TRUE(base::ReadFileToString(
      base::FilePath(kTestDataRoot).Append("progress_0_output"),
      &progress_0_output));
  EXPECT_TRUE(base::ReadFileToString(
      base::FilePath(kTestDataRoot).Append("progress_bit_flip_output"),
      &progress_bit_flip_output));
  EXPECT_TRUE(base::ReadFileToString(
      base::FilePath(kTestDataRoot).Append("all_test_passed_output"),
      &all_test_passed_output));
  // Check that the output are strictly increasing by checking if the outputs
  // are prefix of each other.
  EXPECT_TRUE(base::StartsWith(progress_bit_flip_output, progress_0_output,
                               base::CompareCase::SENSITIVE));
  EXPECT_TRUE(base::StartsWith(all_test_passed_output, progress_bit_flip_output,
                               base::CompareCase::SENSITIVE));

  SetExecutorOutputFromTestFile("progress_0_output");

  routine_->SetOnExceptionCallback(
      base::BindOnce([](uint32_t error, const std::string& reason) {
        CHECK(false) << "An exception has occurred when it shouldn't have.";
      }));
  auto observer = std::make_unique<RoutineObserverImpl>(base::DoNothing());
  routine_->AddObserver(observer->receiver_.BindNewPipeAndPassRemote());
  routine_->Start();

  // Fast forward for observer to update percentage.
  task_environment_.FastForwardBy(kMemoryRoutineUpdatePeriod);
  EXPECT_EQ(observer->state_->percentage, 0);

  SetExecutorOutputFromTestFile("progress_bit_flip_output");

  // Fast forward for observer to update percentage.
  task_environment_.FastForwardBy(kMemoryRoutineUpdatePeriod);
  EXPECT_EQ(observer->state_->percentage, kBitFlipPercentage);

  SetExecutorOutputFromTestFile("all_test_passed_output");
  SetExecutorReturnCode(EXIT_SUCCESS);

  // Fast forward for observer to set finished state.
  task_environment_.FastForwardBy(kMemoryRoutineUpdatePeriod);
  EXPECT_EQ(observer->state_->percentage, 100);
}

}  // namespace
}  // namespace diagnostics
