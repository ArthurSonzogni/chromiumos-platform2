// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/led/led_lit_up.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <base/check.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <base/types/expected.h>

#include "diagnostics/cros_healthd/system/context.h"
#include "diagnostics/cros_healthd/system/ground_truth.h"
#include "diagnostics/mojom/public/cros_healthd_routines.mojom.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

void LogResetColorError(const std::optional<std::string>& err) {
  if (err) {
    LOG(WARNING) << "Reset LED color failed: " << err.value();
  }
}

}  // namespace

base::expected<std::unique_ptr<BaseRoutineControl>, mojom::SupportStatusPtr>
LedLitUpRoutine::Create(Context* context,
                        mojom::LedLitUpRoutineArgumentPtr arg) {
  CHECK(!arg.is_null());

  auto status = context->ground_truth()->PrepareRoutineLedLitUp();
  if (!status->is_supported()) {
    return base::unexpected(std::move(status));
  }
  if (arg->name == mojom::LedName::kUnmappedEnumField) {
    return base::unexpected(mojom::SupportStatus::NewUnsupported(
        mojom::Unsupported::New("Unexpected LED name", /*reason=*/nullptr)));
  }
  if (arg->color == mojom::LedColor::kUnmappedEnumField) {
    return base::unexpected(mojom::SupportStatus::NewUnsupported(
        mojom::Unsupported::New("Unexpected LED color", /*reason=*/nullptr)));
  }
  return base::ok(
      base::WrapUnique(new LedLitUpRoutine(context, std::move(arg))));
}

LedLitUpRoutine::LedLitUpRoutine(Context* context,
                                 mojom::LedLitUpRoutineArgumentPtr arg)
    : context_(context), name_(arg->name), color_(arg->color) {
  CHECK(context_);
  if (arg->replier.is_valid()) {
    // The disconnection of |replier_| is handled in |RunNextStep| to avoid
    // resetting the LED before the specified color is set.
    replier_.Bind(std::move(arg->replier));
  }
}

LedLitUpRoutine::~LedLitUpRoutine() {
  if (need_reset_color_in_cleanup_) {
    context_->executor()->ResetLedColor(name_,
                                        base::BindOnce(&LogResetColorError));
  }
}

void LedLitUpRoutine::OnStart() {
  CHECK_EQ(step_, TestStep::kInitialize);
  if (!replier_.is_bound()) {
    RaiseException("Invalid replier.");
    return;
  }
  RunNextStep();
}

void LedLitUpRoutine::ReplierDisconnectHandler() {
  CHECK_EQ(step_, TestStep::kGetColorMatched);
  context_->executor()->ResetLedColor(name_,
                                      base::BindOnce(&LogResetColorError));
  need_reset_color_in_cleanup_ = false;
  RaiseException("Replier disconnected.");
}

void LedLitUpRoutine::SetLedColorCallback(
    const std::optional<std::string>& err) {
  CHECK_EQ(step_, TestStep::kSetColor);
  if (err) {
    LOG(ERROR) << "Failed to set LED color: " << err.value();
    // Reset the color since there might be error while the color was changed.
    context_->executor()->ResetLedColor(name_,
                                        base::BindOnce(&LogResetColorError));
    RaiseException("Failed to set LED color.");
    return;
  }
  need_reset_color_in_cleanup_ = true;
  RunNextStep();
}

void LedLitUpRoutine::GetColorMatchedCallback(bool matched) {
  CHECK_EQ(step_, TestStep::kGetColorMatched);
  // No need to handle the disconnection after receiving the response.
  replier_.set_disconnect_handler(base::DoNothing());
  color_matched_response_ = matched;
  RunNextStep();
}

void LedLitUpRoutine::ResetLedColorCallback(
    const std::optional<std::string>& err) {
  CHECK_EQ(step_, TestStep::kResetColor);
  // Don't need to reset the color again if we've tried once.
  need_reset_color_in_cleanup_ = false;
  if (err) {
    LOG(ERROR) << "Failed to reset LED color: " << err.value();
    RaiseException("Failed to reset LED color.");
    return;
  }
  RunNextStep();
}

void LedLitUpRoutine::RunNextStep() {
  step_ = static_cast<TestStep>(static_cast<int>(step_) + 1);

  switch (step_) {
    case TestStep::kInitialize:
      RaiseException("Unexpected diagnostic flow.");
      break;
    case TestStep::kSetColor:
      SetRunningState();
      SetPercentage(25);
      context_->executor()->SetLedColor(
          name_, color_,
          base::BindOnce(&LedLitUpRoutine::SetLedColorCallback,
                         weak_ptr_factory_.GetWeakPtr()));
      break;
    case TestStep::kGetColorMatched:
      if (!replier_.is_connected()) {
        // Handle the disconnection before calling the remote function.
        ReplierDisconnectHandler();
      } else {
        SetPercentage(50);
        SetWaitingState(mojom::RoutineStateWaiting::Reason::kWaitingUserInput,
                        "Waiting for user to check the LED color.");
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
      SetRunningState();
      SetPercentage(75);
      context_->executor()->ResetLedColor(
          name_, base::BindOnce(&LedLitUpRoutine::ResetLedColorCallback,
                                weak_ptr_factory_.GetWeakPtr()));
      break;
    case TestStep::kComplete:
      SetFinishedState(/*has_passed*/ color_matched_response_,
                       mojom::RoutineDetail::NewLedLitUp(
                           mojom::LedLitUpRoutineDetail::New()));
      break;
  }
}

}  // namespace diagnostics
