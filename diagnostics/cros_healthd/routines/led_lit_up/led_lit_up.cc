// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/led_lit_up/led_lit_up.h"

#include <utility>

#include <base/callback.h>
#include <base/cancelable_callback.h>
#include <base/logging.h>

#include "diagnostics/mojom/public/cros_healthd_diagnostics.mojom.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

void LogResetColorError(const std::optional<std::string>& err) {
  if (err) {
    LOG(WARNING) << "Reset LED color failed: " << err.value();
  }
}

}  // namespace

LedLitUpRoutine::LedLitUpRoutine(
    Context* context,
    mojom::LedName name,
    mojom::LedColor color,
    mojo::PendingRemote<mojom::LedLitUpRoutineReplier> replier)
    : context_(context),
      name_(name),
      color_(color),
      step_(TestStep::kInitialize),
      status_(mojom::DiagnosticRoutineStatusEnum::kReady) {
  // The disconnection of |replier_| is handled in |RunNextStep| to avoid
  // resetting the LED before the specified color is set.
  replier_.Bind(std::move(replier));
}

LedLitUpRoutine::~LedLitUpRoutine() = default;

void LedLitUpRoutine::Start() {
  RunNextStep();
}

void LedLitUpRoutine::Resume() {}

void LedLitUpRoutine::Cancel() {
  if (status_ == mojom::DiagnosticRoutineStatusEnum::kWaiting) {
    context_->executor()->ResetLedColor(name_,
                                        base::BindOnce(&LogResetColorError));
    status_ = mojom::DiagnosticRoutineStatusEnum::kCancelled;
    status_message_ = "Canceled.";
  }
}

void LedLitUpRoutine::PopulateStatusUpdate(mojom::RoutineUpdate* response,
                                           bool include_output) {
  if (step_ == TestStep::kGetColorMatched) {
    // In this step, the client should check the color of the LED.
    auto interactive_update = mojom::InteractiveRoutineUpdate::New();
    interactive_update->user_message =
        mojom::DiagnosticRoutineUserMessageEnum::kCheckLedColor;
    response->routine_update_union =
        mojom::RoutineUpdateUnion::NewInteractiveUpdate(
            std::move(interactive_update));
  } else {
    auto noninteractive_update = mojom::NonInteractiveRoutineUpdate::New();
    noninteractive_update->status = status_;
    noninteractive_update->status_message = status_message_;

    response->routine_update_union =
        mojom::RoutineUpdateUnion::NewNoninteractiveUpdate(
            std::move(noninteractive_update));
  }

  response->progress_percent = step_ * 100 / TestStep::kComplete;
}

mojom::DiagnosticRoutineStatusEnum LedLitUpRoutine::GetStatus() {
  return status_;
}

void LedLitUpRoutine::ReplierDisconnectHandler() {
  context_->executor()->ResetLedColor(name_,
                                      base::BindOnce(&LogResetColorError));
  status_ = mojom::DiagnosticRoutineStatusEnum::kFailed;
  status_message_ = "Replier disconnected.";
}

void LedLitUpRoutine::SetLedColorCallback(
    const std::optional<std::string>& err) {
  if (err) {
    context_->executor()->ResetLedColor(name_,
                                        base::BindOnce(&LogResetColorError));
    status_ = mojom::DiagnosticRoutineStatusEnum::kFailed;
    status_message_ = err.value();
    return;
  }
  RunNextStep();
}

void LedLitUpRoutine::GetColorMatchedCallback(bool matched) {
  // No need to handle the disconnection after receiving the response.
  replier_.set_disconnect_handler(base::DoNothing());
  color_matched_response_ = matched;
  RunNextStep();
}

void LedLitUpRoutine::ResetLedColorCallback(
    const std::optional<std::string>& err) {
  if (err) {
    status_ = mojom::DiagnosticRoutineStatusEnum::kFailed;
    status_message_ = err.value();
    return;
  }
  RunNextStep();
}

void LedLitUpRoutine::RunNextStep() {
  step_ = static_cast<TestStep>(static_cast<int>(step_) + 1);

  switch (step_) {
    case TestStep::kInitialize:
      status_ = mojom::DiagnosticRoutineStatusEnum::kError;
      status_message_ = "Unexpected LED lit up diagnostic flow.";
      break;
    case TestStep::kSetColor:
      status_ = mojom::DiagnosticRoutineStatusEnum::kRunning;
      context_->executor()->SetLedColor(
          name_, color_,
          base::BindOnce(&LedLitUpRoutine::SetLedColorCallback,
                         weak_ptr_factory_.GetWeakPtr()));
      break;
    case TestStep::kGetColorMatched:
      status_ = mojom::DiagnosticRoutineStatusEnum::kWaiting;
      if (!replier_.is_connected()) {
        // Handle the disconnection before calling the remote function.
        ReplierDisconnectHandler();
      } else {
        // Handle the disconnection during calling the remote function.
        replier_.set_disconnect_handler(
            base::BindOnce(&LedLitUpRoutine::ReplierDisconnectHandler,
                           weak_ptr_factory_.GetWeakPtr()));
        replier_->GetColorMatched(
            base::BindOnce(&LedLitUpRoutine::GetColorMatchedCallback,
                           weak_ptr_factory_.GetWeakPtr()));
      }
      break;
    case TestStep::kResetColor:
      status_ = mojom::DiagnosticRoutineStatusEnum::kRunning;
      context_->executor()->ResetLedColor(
          name_, base::BindOnce(&LedLitUpRoutine::ResetLedColorCallback,
                                weak_ptr_factory_.GetWeakPtr()));
      break;
    case TestStep::kComplete:
      if (color_matched_response_) {
        status_ = mojom::DiagnosticRoutineStatusEnum::kPassed;
        status_message_ = "Routine passed.";
      } else {
        status_ = mojom::DiagnosticRoutineStatusEnum::kFailed;
        status_message_ = "Not lit up in the specified color.";
      }
      break;
  }
}

}  // namespace diagnostics
