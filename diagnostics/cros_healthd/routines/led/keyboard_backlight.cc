// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/led/keyboard_backlight.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/functional/bind.h>
#include <base/functional/callback.h>
#include <base/functional/callback_helpers.h>
#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <base/task/single_thread_task_runner.h>
#include <base/types/expected.h>
#include <brillo/errors/error.h>
#include <power_manager/dbus-proxies.h>
#include <power_manager/proto_bindings/backlight.pb.h>

#include "diagnostics/cros_healthd/system/context.h"
#include "diagnostics/cros_healthd/system/ground_truth.h"
#include "diagnostics/cros_healthd/utils/dbus_utils.h"
#include "diagnostics/mojom/public/cros_healthd_routines.mojom.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

std::optional<std::vector<uint8_t>> GetSetBacklightBrightnessRequestProto(
    double brightness_percent) {
  power_manager::SetBacklightBrightnessRequest request;
  // TODO(b/271818863): Add a cause specific for DIAGNOSTICS.
  request.set_cause(
      power_manager::SetBacklightBrightnessRequest_Cause_USER_REQUEST);
  request.set_percent(brightness_percent);
  std::vector<uint8_t> in_serialized_proto(request.ByteSizeLong());
  if (!request.SerializeToArray(in_serialized_proto.data(),
                                in_serialized_proto.size())) {
    return std::nullopt;
  }
  return std::move(in_serialized_proto);
}

}  // namespace

base::expected<std::unique_ptr<BaseRoutineControl>, mojom::SupportStatusPtr>
KeyboardBacklightRoutine::Create(
    Context* context, mojom::KeyboardBacklightRoutineArgumentPtr arg) {
  auto status = context->ground_truth()->PrepareRoutineKeyboardBacklight();
  if (!status->is_supported()) {
    return base::unexpected(std::move(status));
  }
  return base::ok(base::WrapUnique(new KeyboardBacklightRoutine(context)));
}

KeyboardBacklightRoutine::KeyboardBacklightRoutine(Context* context)
    : context_(context) {
  CHECK(context_);
}

KeyboardBacklightRoutine::~KeyboardBacklightRoutine() = default;

void KeyboardBacklightRoutine::OnStart() {
  CHECK_EQ(step_, TestStep::kInitialize);
  // Save user configurations.
  auto [on_success, on_error] = SplitDbusCallback(
      base::BindOnce(&KeyboardBacklightRoutine::HandleGetBrightnessOnStart,
                     weak_ptr_factory_.GetWeakPtr()));
  context_->power_manager_proxy()->GetKeyboardBrightnessPercentAsync(
      std::move(on_success), std::move(on_error));
}

void KeyboardBacklightRoutine::OnReplyInquiry(
    mojom::RoutineInquiryReplyPtr reply) {
  if (step_ != TestStep::kWaitingForUserConfirmation) {
    RaiseException("Unexpected diagnostic flow.");
    return;
  }
  if (!reply->is_check_keyboard_backlight_state()) {
    RaiseException("Reply type is not check-keyboard-backlight-state.");
    return;
  }
  const auto& reply_state = reply->get_check_keyboard_backlight_state();
  CHECK(!reply_state.is_null());
  switch (reply_state->state) {
    case mojom::CheckKeyboardBacklightStateReply::State::kOk:
      routine_passed_ = true;
      RunNextStep();
      return;
    case mojom::CheckKeyboardBacklightStateReply::State::kAnyNotLitUp:
      routine_passed_ = false;
      RunNextStep();
      return;
    case mojom::CheckKeyboardBacklightStateReply::State::kUnmappedEnumField:
      RaiseException("Unrecognized state value.");
      return;
  }
}

void KeyboardBacklightRoutine::TestBrightness(uint32_t brightness_to_test) {
  if (brightness_to_test > kMaxBrightnessPercentToTest) {
    RunNextStep();
    return;
  }

  std::optional<std::vector<uint8_t>> in_serialized_proto =
      GetSetBacklightBrightnessRequestProto(brightness_to_test);
  if (!in_serialized_proto.has_value()) {
    RaiseException("Could not serialize SetBacklightBrightnessRequest.");
    return;
  }

  auto [on_success, on_error] = SplitDbusCallback(base::BindOnce(
      &KeyboardBacklightRoutine::HandleSetBrightnessDuringTesting,
      weak_ptr_factory_.GetWeakPtr(), brightness_to_test));
  context_->power_manager_proxy()->SetKeyboardBrightnessAsync(
      in_serialized_proto.value(), std::move(on_success), std::move(on_error));
}

void KeyboardBacklightRoutine::RestoreConfig() {
  // Brightness restoration must be done before enable ALS to avoid setting ALS
  // off again.
  std::optional<std::vector<uint8_t>> in_serialized_proto =
      GetSetBacklightBrightnessRequestProto(brightness_percent_on_start_);
  if (!in_serialized_proto.has_value()) {
    RaiseException("Could not serialize SetBacklightBrightnessRequest.");
    return;
  }
  auto [on_success, on_error] = SplitDbusCallback(
      base::BindOnce(&KeyboardBacklightRoutine::HandleRestoreBrightness,
                     weak_ptr_factory_.GetWeakPtr()));
  context_->power_manager_proxy()->SetKeyboardBrightnessAsync(
      in_serialized_proto.value(), std::move(on_success), std::move(on_error));
}

void KeyboardBacklightRoutine::HandleGetBrightnessOnStart(brillo::Error* err,
                                                          double percent) {
  if (err) {
    RaiseException("Failed to get brightness.");
    return;
  }

  brightness_percent_on_start_ = std::clamp(percent, 0.0, 100.0);

  // We only enable the ambient light sensor (ALS) in case of crash since users
  // can adjust the brightness easily but ALS cannot be controlled by users.
  // Set up a base scoped closure runner for enabling ALS. We expect the context
  // pointer to live as long as healthd lives, therefore it is safe to pass
  // `context_` directly. We cannot guarantee the routine object still exists
  // when this callback is called, therefore we do not wait for its callback.
  enable_als_closure_ = base::ScopedClosureRunner(base::BindOnce(
      [](Context* context) {
        context->power_manager_proxy()
            ->SetKeyboardAmbientLightSensorEnabledAsync(true, base::DoNothing(),
                                                        base::DoNothing());
      },
      context_));

  RunNextStep();
}

void KeyboardBacklightRoutine::HandleSetBrightnessDuringTesting(
    uint32_t brightness_to_test, brillo::Error* err) {
  if (err) {
    RaiseException("Failed to set brightness.");
    return;
  }

  // Test next brightness percent in `kTimeToStayAtEachPercent` secs.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&KeyboardBacklightRoutine::TestBrightness,
                     weak_ptr_factory_.GetWeakPtr(),
                     brightness_to_test + kBrightnessPercentToTestIncrement),
      kTimeToStayAtEachPercent);
}

void KeyboardBacklightRoutine::HandleRestoreBrightness(brillo::Error* err) {
  if (err) {
    RaiseException("Failed to restore brightness.");
    return;
  }

  // Now enable the ALS after brightness restoration.
  enable_als_closure_.RunAndReset();

  RunNextStep();
}

void KeyboardBacklightRoutine::RunNextStep() {
  step_ = static_cast<TestStep>(static_cast<int>(step_) + 1);
  switch (step_) {
    case TestStep::kInitialize:
      RaiseException("Unexpected diagnostic flow.");
      break;
    case TestStep::kTestBrightness:
      SetRunningState();
      SetPercentage(25);
      TestBrightness(kMinBrightnessPercentToTest);
      break;
    case TestStep::kWaitingForUserConfirmation:
      SetPercentage(50);
      SetWaitingInquiryState(
          "Waiting for user to confirm the correctness of brightness.",
          mojom::RoutineInquiry::NewCheckKeyboardBacklightState(
              mojom::CheckKeyboardBacklightStateInquiry::New()));
      break;
    case TestStep::kRestoreConfig:
      SetRunningState();
      SetPercentage(75);
      RestoreConfig();
      break;
    case TestStep::kComplete:
      SetFinishedState(/*has_passed=*/routine_passed_,
                       /*detail=*/nullptr);
      break;
  }
}

}  // namespace diagnostics
