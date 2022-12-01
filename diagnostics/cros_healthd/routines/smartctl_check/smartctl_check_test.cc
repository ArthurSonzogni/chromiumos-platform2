// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>

#include <base/base64.h>
#include <base/check.h>
#include <base/json/json_reader.h>
#include <base/strings/stringprintf.h>
#include <debugd/dbus-proxy-mocks.h>
#include <base/files/file_path.h>
#include <base/files/scoped_temp_dir.h>
#include <gtest/gtest.h>

#include "diagnostics/common/mojo_utils.h"
#include "diagnostics/cros_healthd/routines/routine_test_utils.h"
#include "diagnostics/cros_healthd/routines/smartctl_check/smartctl_check.h"
#include "diagnostics/mojom/public/cros_healthd_diagnostics.mojom.h"

namespace diagnostics {
namespace {

namespace mojom = ::ash::cros_healthd::mojom;
using OnceStringCallback = base::OnceCallback<void(const std::string& result)>;
using OnceErrorCallback = base::OnceCallback<void(brillo::Error* error)>;
using ::testing::_;
using ::testing::StrictMock;
using ::testing::WithArg;

constexpr char kSmartctlOutputFormat[] =
    "\nAvailable Spare: %d%%\nAvailable Spare Threshold: %d%%";

std::string GetFakeSmartctlOutput(const int available_spare,
                                  const int available_spare_threshold) {
  return base::StringPrintf(kSmartctlOutputFormat, available_spare,
                            available_spare_threshold);
}

void VerifyOutput(mojo::ScopedHandle handle,
                  const int expected_available_spare,
                  const int expected_available_spare_threshold) {
  ASSERT_TRUE(handle->is_valid());
  const auto& shm_mapping =
      diagnostics::GetReadOnlySharedMemoryMappingFromMojoHandle(
          std::move(handle));
  ASSERT_TRUE(shm_mapping.IsValid());
  const auto& json_output = base::JSONReader::Read(std::string(
      shm_mapping.GetMemoryAs<const char>(), shm_mapping.mapped_size()));
  const auto& output_dict = json_output->GetIfDict();
  ASSERT_NE(output_dict, nullptr);

  const auto& result_details = output_dict->FindDict("resultDetails");
  ASSERT_NE(result_details, nullptr);
  ASSERT_EQ(result_details->FindInt("availableSpare"),
            expected_available_spare);
  ASSERT_EQ(result_details->FindInt("availableSpareThreshold"),
            expected_available_spare_threshold);
}

class SmartctlCheckRoutineTest : public testing::Test {
 protected:
  SmartctlCheckRoutineTest() = default;
  SmartctlCheckRoutineTest(const SmartctlCheckRoutineTest&) = delete;
  SmartctlCheckRoutineTest& operator=(const SmartctlCheckRoutineTest&) = delete;

  DiagnosticRoutine* routine() { return routine_.get(); }

  void CreateSmartctlCheckRoutine() {
    routine_ = std::make_unique<SmartctlCheckRoutine>(&debugd_proxy_);
  }

  mojom::RoutineUpdatePtr RunRoutineAndWaitForExit() {
    DCHECK(routine_);
    mojom::RoutineUpdate update{0, mojo::ScopedHandle(),
                                mojom::RoutineUpdateUnionPtr()};

    routine_->Start();
    routine_->PopulateStatusUpdate(&update, true);
    return mojom::RoutineUpdate::New(update.progress_percent,
                                     std::move(update.output),
                                     std::move(update.routine_update_union));
  }

  StrictMock<org::chromium::debugdProxyMock> debugd_proxy_;

 private:
  std::unique_ptr<SmartctlCheckRoutine> routine_;
};

// Tests that the SmartctlCheck routine passes if available_spare is greater
// than the available_spare_threshold.
TEST_F(SmartctlCheckRoutineTest, Pass) {
  int available_spare = 100;
  int available_spare_threshold = 5;

  CreateSmartctlCheckRoutine();
  EXPECT_CALL(debugd_proxy_, SmartctlAsync("attributes", _, _, _))
      .WillOnce(WithArg<1>([&](OnceStringCallback callback) {
        std::move(callback).Run(
            GetFakeSmartctlOutput(available_spare, available_spare_threshold));
      }));
  EXPECT_EQ(routine()->GetStatus(), mojom::DiagnosticRoutineStatusEnum::kReady);

  const auto& routine_update = RunRoutineAndWaitForExit();
  VerifyNonInteractiveUpdate(routine_update->routine_update_union,
                             mojom::DiagnosticRoutineStatusEnum::kPassed,
                             kSmartctlCheckRoutineSuccess);
  VerifyOutput(std::move(routine_update->output), available_spare,
               available_spare_threshold);
}

// Tests that the SmartctlCheck routine fails if available_spare is below the
// available_spare_threshold.
TEST_F(SmartctlCheckRoutineTest, AvailableSpareBelowThreshold) {
  int available_spare = 1;
  int available_spare_threshold = 5;

  CreateSmartctlCheckRoutine();
  EXPECT_CALL(debugd_proxy_, SmartctlAsync("attributes", _, _, _))
      .WillOnce(WithArg<1>([&](OnceStringCallback callback) {
        std::move(callback).Run(
            GetFakeSmartctlOutput(available_spare, available_spare_threshold));
      }));
  EXPECT_EQ(routine()->GetStatus(), mojom::DiagnosticRoutineStatusEnum::kReady);

  const auto& routine_update = RunRoutineAndWaitForExit();
  VerifyNonInteractiveUpdate(routine_update->routine_update_union,
                             mojom::DiagnosticRoutineStatusEnum::kFailed,
                             kSmartctlCheckRoutineFailedAvailableSpare);
  VerifyOutput(std::move(routine_update->output), available_spare,
               available_spare_threshold);
}

// Tests that the SmartctlCheck routine fails if debugd proxy returns
// invalid data.
TEST_F(SmartctlCheckRoutineTest, InvalidDebugdData) {
  CreateSmartctlCheckRoutine();
  EXPECT_CALL(debugd_proxy_, SmartctlAsync("attributes", _, _, _))
      .WillOnce(WithArg<1>(
          [&](OnceStringCallback callback) { std::move(callback).Run(""); }));

  VerifyNonInteractiveUpdate(RunRoutineAndWaitForExit()->routine_update_union,
                             mojom::DiagnosticRoutineStatusEnum::kFailed,
                             kSmartctlCheckRoutineFailedToParse);
}

// Tests that the SmartctlCheck routine returns error if debugd returns with an
// error.
TEST_F(SmartctlCheckRoutineTest, DebugdError) {
  const char kDebugdErrorMessage[] = "Debugd mock error for testing";
  const brillo::ErrorPtr kError =
      brillo::Error::Create(FROM_HERE, "", "", kDebugdErrorMessage);
  CreateSmartctlCheckRoutine();
  EXPECT_CALL(debugd_proxy_, SmartctlAsync("attributes", _, _, _))
      .WillOnce(WithArg<2>([&](OnceErrorCallback callback) {
        std::move(callback).Run(kError.get());
      }));
  VerifyNonInteractiveUpdate(RunRoutineAndWaitForExit()->routine_update_union,
                             mojom::DiagnosticRoutineStatusEnum::kError,
                             kSmartctlCheckRoutineDebugdError);
}

}  // namespace
}  // namespace diagnostics
