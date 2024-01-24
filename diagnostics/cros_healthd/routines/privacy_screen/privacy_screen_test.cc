// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/privacy_screen/privacy_screen.h"

#include <memory>
#include <optional>
#include <utility>

#include <base/functional/bind.h>
#include <base/task/sequenced_task_runner.h>
#include <base/test/task_environment.h>
#include <base/task/thread_pool/thread_pool_instance.h>
#include <base/test/bind.h>
#include <base/test/gmock_callback_support.h>
#include <base/time/time.h>

#include "diagnostics/cros_healthd/mojom/executor.mojom.h"
#include "diagnostics/cros_healthd/routines/routine_test_utils.h"
#include "diagnostics/cros_healthd/system/fake_mojo_service.h"
#include "diagnostics/cros_healthd/system/mock_context.h"

namespace diagnostics {
namespace {

namespace mojom = ::ash::cros_healthd::mojom;

using ::testing::_;

const char kDelegateError[] = "Delegate error";

class PrivacyScreenRoutineTest : public ::testing::Test {
 public:
  PrivacyScreenRoutineTest(const PrivacyScreenRoutineTest&) = delete;
  PrivacyScreenRoutineTest& operator=(const PrivacyScreenRoutineTest&) = delete;

 protected:
  PrivacyScreenRoutineTest() = default;

  void SetUp() override {
    context_.fake_mojo_service()->InitializeFakeMojoService();
  }

  void CreateRoutine(bool target_state) {
    routine_ = std::make_unique<PrivacyScreenRoutine>(&context_, target_state);
  }

  void SetRoutineDestiny(bool display_util_init_success,
                         bool privacy_screen_supported,
                         bool privacy_screen_enabled_before,
                         std::optional<bool> privacy_screen_request_processed,
                         bool privacy_screen_enabled_after) {
    SetDelegateDestiny(display_util_init_success, privacy_screen_supported,
                       privacy_screen_enabled_before);
    context_.fake_mojo_service()
        ->fake_chromium_data_collector()
        .SetPrivacyScreenRequestProcessedBehaviour(
            base::BindOnce(&PrivacyScreenRoutineTest::SetDelegateDestiny,
                           base::Unretained(this), display_util_init_success,
                           privacy_screen_supported,
                           privacy_screen_enabled_after),
            base::Milliseconds(
                // The timeout in routine is 1000 ms, and the unittest checks
                // the result in 2000 ms; therefore we set a delay between those
                // values when browser response timeout exceeds.
                privacy_screen_request_processed.has_value() ? 10 : 1500),
            privacy_screen_request_processed.value_or(true));
  }

  void RunRoutineAndWaitUntilFinished() {
    routine_->Start();
    // Privacy screen routine should be finished within 1 second. Set 2 seconds
    // as a safe timeout.
    task_environment_.FastForwardBy(base::Seconds(2));
  }

  mojom::RoutineUpdatePtr GetUpdate() {
    mojom::RoutineUpdate update{0, mojo::ScopedHandle(),
                                mojom::RoutineUpdateUnionPtr()};
    routine_->PopulateStatusUpdate(&update, true);
    return mojom::RoutineUpdate::New(update.progress_percent,
                                     std::move(update.output),
                                     std::move(update.routine_update_union));
  }

 private:
  void SetDelegateDestiny(bool initialization_success,
                          bool privacy_screen_supported,
                          bool privacy_screen_enabled) {
    if (initialization_success) {
      EXPECT_CALL(*context_.mock_executor(), GetPrivacyScreenInfo(_))
          .WillRepeatedly(
              [=](MockExecutor::GetPrivacyScreenInfoCallback callback) {
                auto info = mojom::PrivacyScreenInfo::New();
                info->privacy_screen_enabled = privacy_screen_enabled;
                info->privacy_screen_supported = privacy_screen_supported;
                std::move(callback).Run(
                    mojom::GetPrivacyScreenInfoResult::NewInfo(
                        std::move(info)));
              });
    } else {
      EXPECT_CALL(*context_.mock_executor(), GetPrivacyScreenInfo(_))
          .WillRepeatedly(
              [](MockExecutor::GetPrivacyScreenInfoCallback callback) {
                std::move(callback).Run(
                    mojom::GetPrivacyScreenInfoResult::NewError(
                        kDelegateError));
              });
    }
  }

  MockContext context_;
  std::unique_ptr<PrivacyScreenRoutine> routine_;
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
};

// Test that routine error occurs if there is an error in executor delegate.
TEST_F(PrivacyScreenRoutineTest, ErrorIfExecutorDelegateEncountersErrors) {
  CreateRoutine(/*target_state=*/true);
  SetRoutineDestiny(/*display_util_init_success=*/false,
                    /*privacy_screen_supported=*/true,
                    /*privacy_screen_enabled_before=*/false,
                    /*privacy_screen_request_processed=*/true,
                    /*privacy_screen_enabled_after=*/true);
  RunRoutineAndWaitUntilFinished();
  VerifyNonInteractiveUpdate(GetUpdate()->routine_update_union,
                             mojom::DiagnosticRoutineStatusEnum::kError,
                             kDelegateError);
}

// Test that routine fails if browser rejects request.
TEST_F(PrivacyScreenRoutineTest, RequestRejected) {
  CreateRoutine(/*target_state=*/true);
  SetRoutineDestiny(/*display_util_init_success=*/true,
                    /*privacy_screen_supported=*/true,
                    /*privacy_screen_enabled_before=*/false,
                    /*privacy_screen_request_processed=*/false,
                    /*privacy_screen_enabled_after=*/false);
  RunRoutineAndWaitUntilFinished();
  VerifyNonInteractiveUpdate(GetUpdate()->routine_update_union,
                             mojom::DiagnosticRoutineStatusEnum::kFailed,
                             kPrivacyScreenRoutineRequestRejectedMessage);
}

// Test that routine fails if browser response timeout exceeds.
TEST_F(PrivacyScreenRoutineTest, BrowserResponseTimeoutExceeded) {
  CreateRoutine(/*target_state=*/true);
  SetRoutineDestiny(/*display_util_init_success=*/true,
                    /*privacy_screen_supported=*/true,
                    /*privacy_screen_enabled_before=*/false,
                    /*privacy_screen_request_processed=*/std::nullopt,
                    /*privacy_screen_enabled_after=*/true);
  RunRoutineAndWaitUntilFinished();
  VerifyNonInteractiveUpdate(
      GetUpdate()->routine_update_union,
      mojom::DiagnosticRoutineStatusEnum::kFailed,
      kPrivacyScreenRoutineBrowserResponseTimeoutExceededMessage);
}

// Test that routine fails if privacy screen is not turned on.
TEST_F(PrivacyScreenRoutineTest, TurnOnFailed) {
  CreateRoutine(/*target_state=*/true);
  SetRoutineDestiny(/*display_util_init_success=*/true,
                    /*privacy_screen_supported=*/true,
                    /*privacy_screen_enabled_before=*/false,
                    /*privacy_screen_request_processed=*/true,
                    /*privacy_screen_enabled_after=*/false);
  RunRoutineAndWaitUntilFinished();
  VerifyNonInteractiveUpdate(
      GetUpdate()->routine_update_union,
      mojom::DiagnosticRoutineStatusEnum::kFailed,
      kPrivacyScreenRoutineFailedToTurnPrivacyScreenOnMessage);
}

// Test that routine fails if privacy screen is not turned off.
TEST_F(PrivacyScreenRoutineTest, TurnOffFailed) {
  CreateRoutine(/*target_state=*/false);
  SetRoutineDestiny(/*display_util_init_success=*/true,
                    /*privacy_screen_supported=*/true,
                    /*privacy_screen_enabled_before=*/true,
                    /*privacy_screen_request_processed=*/true,
                    /*privacy_screen_enabled_after=*/true);
  RunRoutineAndWaitUntilFinished();
  VerifyNonInteractiveUpdate(
      GetUpdate()->routine_update_union,
      mojom::DiagnosticRoutineStatusEnum::kFailed,
      kPrivacyScreenRoutineFailedToTurnPrivacyScreenOffMessage);
}

// Test that we can turn privacy screen on.
TEST_F(PrivacyScreenRoutineTest, TurnOnSuccess) {
  CreateRoutine(/*target_state=*/true);
  SetRoutineDestiny(/*display_util_init_success=*/true,
                    /*privacy_screen_supported=*/true,
                    /*privacy_screen_enabled_before=*/false,
                    /*privacy_screen_request_processed=*/true,
                    /*privacy_screen_enabled_after=*/true);
  RunRoutineAndWaitUntilFinished();
  VerifyNonInteractiveUpdate(GetUpdate()->routine_update_union,
                             mojom::DiagnosticRoutineStatusEnum::kPassed,
                             kPrivacyScreenRoutineSucceededMessage);
}

// Test that we can turn privacy screen off.
TEST_F(PrivacyScreenRoutineTest, TurnOffSuccess) {
  CreateRoutine(/*target_state=*/false);
  SetRoutineDestiny(/*display_util_init_success=*/true,
                    /*privacy_screen_supported=*/true,
                    /*privacy_screen_enabled_before=*/true,
                    /*privacy_screen_request_processed=*/true,
                    /*privacy_screen_enabled_after=*/false);
  RunRoutineAndWaitUntilFinished();
  VerifyNonInteractiveUpdate(GetUpdate()->routine_update_union,
                             mojom::DiagnosticRoutineStatusEnum::kPassed,
                             kPrivacyScreenRoutineSucceededMessage);
}

}  // namespace
}  // namespace diagnostics
