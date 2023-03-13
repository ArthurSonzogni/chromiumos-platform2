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
#include <gmock/gmock-function-mocker.h>
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
#include "diagnostics/mojom/public/cros_healthd_routines.mojom-forward.h"

namespace diagnostics {
namespace {

namespace mojom = ash::cros_healthd::mojom;
using ::testing::_;
using ::testing::Invoke;
using ::testing::WithArg;

// Location of files containing test data (fake memtester output).
constexpr char kTestDataRoot[] =
    "cros_healthd/routines/memory_and_cpu/testdata";

class RoutineObserverImpl : public mojom::RoutineObserver {
 public:
  explicit RoutineObserverImpl(base::OnceClosure on_finished)
      : receiver_{this /* impl */}, on_finished_(std::move(on_finished)) {}
  RoutineObserverImpl(const RoutineObserverImpl&) = delete;
  RoutineObserverImpl& operator=(const RoutineObserverImpl&) = delete;
  ~RoutineObserverImpl() override = default;

  void OnRoutineStateChange(mojom::RoutineStatePtr state) override {
    if (state->state_union->is_finished()) {
      DCHECK(on_finished_);
      result_ = std::move(state);
      std::move(on_finished_).Run();
    }
  }

  mojom::RoutineStatePtr result_;
  mojo::Receiver<mojom::RoutineObserver> receiver_;

 private:
  base::OnceClosure on_finished_;
};

class MemoryRoutineV2Test : public BaseFileTest {
 protected:
  MemoryRoutineV2Test() = default;
  MemoryRoutineV2Test(const MemoryRoutineV2Test&) = delete;
  MemoryRoutineV2Test& operator=(const MemoryRoutineV2Test&) = delete;

  void SetUp() override {
    SetTestRoot(mock_context_.root_dir());
    SetMockMemoryInfo(
        "MemTotal:        3906320 kB\n"
        "MemFree:         2873180 kB\n"
        "MemAvailable:    2878980 kB\n");
  }

  mojom::RoutineStatePtr RunRoutineAndWaitForExit() {
    base::RunLoop run_loop;
    routine_.SetOnExceptionCallback(
        base::BindOnce([](uint32_t error, const std::string& reason) {
          CHECK(false) << "An exception has occurred when it shouldn't have.";
        }));
    mojo::PendingRemote<mojom::RoutineObserver> remote;
    auto observer_ = std::make_unique<RoutineObserverImpl>(
        base::BindOnce(run_loop.QuitClosure()));
    routine_.AddObserver(observer_->receiver_.BindNewPipeAndPassRemote());
    routine_.Start();
    run_loop.Run();
    return std::move(observer_->result_);
  }

  void RunRoutineAndWaitForException() {
    base::RunLoop run_loop;
    routine_.SetOnExceptionCallback(base::BindLambdaForTesting(
        [&](uint32_t error, const std::string& reason) { run_loop.Quit(); }));
    routine_.Start();
    run_loop.Run();
  }

  void SetMockMemoryInfo(const std::string& info) {
    SetFile({"proc", "meminfo"}, info);
  }

  std::set<mojom::MemtesterTestItemEnum> VectorToSet(
      std::vector<mojom::MemtesterTestItemEnum> v) {
    return std::set<mojom::MemtesterTestItemEnum>(v.begin(), v.end());
  }

  std::set<mojom::MemtesterTestItemEnum> GetAllMemtesterTests() {
    return std::set<mojom::MemtesterTestItemEnum>{
        mojom::MemtesterTestItemEnum::kStuckAddress,
        mojom::MemtesterTestItemEnum::kCompareAND,
        mojom::MemtesterTestItemEnum::kCompareDIV,
        mojom::MemtesterTestItemEnum::kCompareMUL,
        mojom::MemtesterTestItemEnum::kCompareOR,
        mojom::MemtesterTestItemEnum::kCompareSUB,
        mojom::MemtesterTestItemEnum::kCompareXOR,
        mojom::MemtesterTestItemEnum::kSequentialIncrement,
        mojom::MemtesterTestItemEnum::kBitFlip,
        mojom::MemtesterTestItemEnum::kBitSpread,
        mojom::MemtesterTestItemEnum::kBlockSequential,
        mojom::MemtesterTestItemEnum::kCheckerboard,
        mojom::MemtesterTestItemEnum::kRandomValue,
        mojom::MemtesterTestItemEnum::kSolidBits,
        mojom::MemtesterTestItemEnum::kWalkingOnes,
        mojom::MemtesterTestItemEnum::kWalkingZeroes,
        mojom::MemtesterTestItemEnum::k8BitWrites,
        mojom::MemtesterTestItemEnum::k16BitWrites};
  }

  // Sets the return code, stdout file handle and stderr file handle based on
  // return_code, outfile_name and errfile_name.
  void SetExecutorResponse(int32_t return_code,
                           const std::string& outfile_content,
                           const std::string& errfile_content) {
    EXPECT_CALL(*mock_context_.mock_executor(), RunMemtesterV2(_, _))
        .WillOnce(WithArg<1>(Invoke(
            [=](mojo::PendingReceiver<ash::cros_healthd::mojom::ProcessControl>
                    receiver) {
              fake_process_control_.BindReceiver(std::move(receiver));
              fake_process_control_.SetReturnCode(return_code);
              fake_process_control_.SetStdoutFileContent(outfile_content);
              fake_process_control_.SetStderrFileContent(errfile_content);
            })));
  }

 protected:
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  MockContext mock_context_;
  FakeProcessControl fake_process_control_;
  MemoryRoutineV2 routine_{&mock_context_};

  base::WeakPtrFactory<MemoryRoutineV2Test> weak_ptr_factory_{this};
};

// Test that the memory routine can run successfully.
TEST_F(MemoryRoutineV2Test, RoutineSuccess) {
  std::string stdout_content;
  EXPECT_TRUE(base::ReadFileToString(
      base::FilePath(kTestDataRoot).Append("all_test_passed_output"),
      &stdout_content));
  SetExecutorResponse(EXIT_SUCCESS, stdout_content, "");
  mojom::RoutineStatePtr result = RunRoutineAndWaitForExit();
  EXPECT_EQ(result->percentage, 100);
  EXPECT_TRUE(result->state_union->is_finished());
  EXPECT_TRUE(result->state_union->get_finished()->has_passed);
  std::set<mojom::MemtesterTestItemEnum> expected_passed =
      GetAllMemtesterTests();
  std::set<mojom::MemtesterTestItemEnum> expected_failed{};
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
  base::RunLoop run_loop;
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
  std::string stdout_content;
  EXPECT_TRUE(base::ReadFileToString(
      base::FilePath(kTestDataRoot).Append("all_test_passed_output"),
      &stdout_content));
  SetExecutorResponse(EXIT_SUCCESS, stdout_content, "");

  mojom::RoutineStatePtr result = RunRoutineAndWaitForExit();
  EXPECT_EQ(result->percentage, 100);
  EXPECT_TRUE(result->state_union->is_finished());
  EXPECT_TRUE(result->state_union->get_finished()->has_passed);
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
  SetExecutorResponse(MemtesterErrorCodes::kAllocatingLockingInvokingError, "",
                      "");
  RunRoutineAndWaitForException();
}

// Test that the memory routine handles a stuck address failure.
TEST_F(MemoryRoutineV2Test, StuckAddressFailure) {
  std::string stdout_content;
  EXPECT_TRUE(base::ReadFileToString(
      base::FilePath(kTestDataRoot).Append("stuck_address_failed_output"),
      &stdout_content));
  SetExecutorResponse(MemtesterErrorCodes::kStuckAddressTestError,
                      stdout_content, "");
  mojom::RoutineStatePtr result = RunRoutineAndWaitForExit();
  EXPECT_EQ(result->percentage, 100);
  EXPECT_TRUE(result->state_union->is_finished());
  EXPECT_FALSE(result->state_union->get_finished()->has_passed);
  std::set<mojom::MemtesterTestItemEnum> expected_passed =
      GetAllMemtesterTests();
  expected_passed.erase(mojom::MemtesterTestItemEnum::kStuckAddress);
  std::set<mojom::MemtesterTestItemEnum> expected_failed{
      mojom::MemtesterTestItemEnum::kStuckAddress};
  EXPECT_EQ(VectorToSet(result->state_union->get_finished()
                            ->detail->get_memory()
                            ->result->passed_items),
            expected_passed);
  EXPECT_EQ(VectorToSet(result->state_union->get_finished()
                            ->detail->get_memory()
                            ->result->failed_items),
            expected_failed);
}

// Test that the memory routine handles a stuck address failure.
TEST_F(MemoryRoutineV2Test, MultipleTestFailure) {
  std::string stdout_content;
  EXPECT_TRUE(base::ReadFileToString(
      base::FilePath(kTestDataRoot)
          .Append("stuck_address_and_bit_flip_failed_output"),
      &stdout_content));
  SetExecutorResponse(MemtesterErrorCodes::kStuckAddressTestError |
                          MemtesterErrorCodes::kOtherTestError,
                      stdout_content, "");
  mojom::RoutineStatePtr result = RunRoutineAndWaitForExit();
  EXPECT_EQ(result->percentage, 100);
  EXPECT_TRUE(result->state_union->is_finished());
  EXPECT_FALSE(result->state_union->get_finished()->has_passed);
  std::set<mojom::MemtesterTestItemEnum> expected_passed =
      GetAllMemtesterTests();
  expected_passed.erase(mojom::MemtesterTestItemEnum::kStuckAddress);
  expected_passed.erase(mojom::MemtesterTestItemEnum::kBitFlip);
  std::set<mojom::MemtesterTestItemEnum> expected_failed{
      mojom::MemtesterTestItemEnum::kStuckAddress,
      mojom::MemtesterTestItemEnum::kBitFlip};
  EXPECT_EQ(VectorToSet(result->state_union->get_finished()
                            ->detail->get_memory()
                            ->result->passed_items),
            expected_passed);
  EXPECT_EQ(VectorToSet(result->state_union->get_finished()
                            ->detail->get_memory()
                            ->result->failed_items),
            expected_failed);
}

}  // namespace
}  // namespace diagnostics
