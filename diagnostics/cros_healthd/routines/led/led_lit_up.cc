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
}

LedLitUpRoutine::~LedLitUpRoutine() {
  if (need_reset_color_in_cleanup_) {
    context_->executor()->ResetLedColor(name_,
                                        base::BindOnce(&LogResetColorError));
  }
}

void LedLitUpRoutine::OnStart() {
  CHECK_EQ(step_, TestStep::kInitialize);
  RunNextStep();
}

void LedLitUpRoutine::OnReplyInquiry(mojom::RoutineInquiryReplyPtr reply) {
  if (step_ != TestStep::kWaitingForLedState) {
    RaiseException("Unexpected diagnostic flow.");
    return;
  }
  if (!reply->is_check_led_lit_up_state()) {
    RaiseException("Reply type is not check-led-lit-up-state.");
    return;
  }
  const auto& led_reply = reply->get_check_led_lit_up_state();
  CHECK(!led_reply.is_null());
  switch (led_reply->state) {
    case mojom::CheckLedLitUpStateReply::State::kCorrectColor:
      led_color_correct_ = true;
      RunNextStep();
      return;
    case mojom::CheckLedLitUpStateReply::State::kNotLitUp:
      led_color_correct_ = false;
      RunNextStep();
      return;
    case mojom::CheckLedLitUpStateReply::State::kUnmappedEnumField:
      RaiseException("Unrecognized LED state value.");
      return;
  }
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
    case TestStep::kWaitingForLedState:
      SetPercentage(50);
      SetWaitingInquiryState("Waiting for user to check the LED color.",
                             mojom::RoutineInquiry::NewCheckLedLitUpState(
                                 mojom::CheckLedLitUpStateInquiry::New()));
      break;
    case TestStep::kResetColor:
      SetRunningState();
      SetPercentage(75);
      context_->executor()->ResetLedColor(
          name_, base::BindOnce(&LedLitUpRoutine::ResetLedColorCallback,
                                weak_ptr_factory_.GetWeakPtr()));
      break;
    case TestStep::kComplete:
      SetFinishedState(/*has_passed=*/led_color_correct_,
                       /*detail=*/nullptr);
      break;
  }
}

}  // namespace diagnostics
