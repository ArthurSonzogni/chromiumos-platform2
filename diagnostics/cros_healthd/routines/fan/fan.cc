// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/fan/fan.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include <base/containers/flat_map.h>
#include <base/functional/bind.h>
#include <base/functional/callback_helpers.h>
#include <base/notreached.h>
#include <base/numerics/safe_conversions.h>
#include <base/strings/string_number_conversions.h>
#include <base/task/single_thread_task_runner.h>
#include <base/time/time.h>

#include "diagnostics/cros_healthd/system/ground_truth.h"
#include "diagnostics/cros_healthd/utils/resource_queue.h"
#include "diagnostics/mojom/public/cros_healthd_routines.mojom.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

}  // namespace

base::expected<std::unique_ptr<BaseRoutineControl>,
               ash::cros_healthd::mojom::SupportStatusPtr>
FanRoutine::Create(Context* context, const mojom::FanRoutineArgumentPtr& arg) {
  CHECK(!arg.is_null());

  uint8_t expected_fan_count;
  auto status = context->ground_truth()->PrepareRoutineFan(expected_fan_count);
  if (!status->is_supported()) {
    return base::unexpected(std::move(status));
  }
  return base::ok(
      base::WrapUnique(new FanRoutine(context, expected_fan_count)));
}

FanRoutine::FanRoutine(Context* context, uint8_t expected_fan_count)
    : context_(context), expected_fan_count_(expected_fan_count) {
  CHECK(context_);
}

FanRoutine::~FanRoutine() = default;

void FanRoutine::OnStart() {
  CHECK(stage_ == Stage::kInitialize);
  SetWaitingState(mojom::RoutineStateWaiting::Reason::kWaitingToBeScheduled,
                  "Waiting for memory and CPU resource");
  // We should not run the fan diag alongside any memory or cpu intensive
  // routine. We expect there to be high variation in the fan speed due to high
  // system load if there is another memory or cpu intensive routine running,
  // and therefore the results will not be accurate.
  context_->memory_cpu_resource_queue()->Enqueue(
      base::BindOnce(&FanRoutine::Run, weak_ptr_factory_.GetWeakPtr()));
}

void FanRoutine::Run(base::ScopedClosureRunner notify_resource_queue_finished) {
  // Set up a base scoped closure runner for resetting the fan control to auto.
  // We expect the context pointer to live as long as healthd lives, therefore
  // it is safe to pass |context_| directly. We cannot guarantee the fan routine
  // object still exists when |reset_fan_control_| is called, therefore we do
  // not wait for its callback.
  reset_fan_control_ = base::ScopedClosureRunner(base::BindOnce(
      [](Context* context) {
        context->executor()->SetAllFanAutoControl(base::DoNothing());
      },
      context_));
  notify_resource_queue_finished_ = std::move(notify_resource_queue_finished);

  SetRunningState();

  stage_ = Stage::kSetIncrease;
  GetFanSpeedHelper();
}

void FanRoutine::GetFanSpeedHelper() {
  context_->executor()->GetAllFanSpeed(base::BindOnce(
      &FanRoutine::HandleGetFanSpeed, weak_ptr_factory_.GetWeakPtr()));
}

void FanRoutine::DelayGetFanSpeed() {
  // Wait `kFanRoutineUpdatePeriod` and attempt to get the fan speed.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&FanRoutine::GetFanSpeedHelper,
                     weak_ptr_factory_.GetWeakPtr()),
      kFanRoutineUpdatePeriod);
}

void FanRoutine::HandleGetFanSpeed(const std::vector<uint16_t>& fan_speed,
                                   const std::optional<std::string>& error) {
  if (error.has_value()) {
    ReleaseResources();
    RaiseException(error.value());
    return;
  }

  switch (stage_) {
    case Stage::kInitialize: {
      LOG(ERROR) << "Routine should not be in initialized after run";
      RaiseException("Invalid routine stage");
      return;
    }
    case Stage::kSetIncrease: {
      base::flat_map<uint8_t, uint16_t> set_fan_rpm = {};
      SetPercentage(10);
      // This stage is achieved when we first receive the current fan speed.
      // Record down this speed as the original fan speed.
      original_fan_speed_ = std::move(fan_speed);

      for (int id = 0; id < original_fan_speed_.size(); ++id) {
        remaining_fan_ids_.insert(id);
        set_fan_rpm.insert({id, original_fan_speed_[id] + kFanRpmChange});
      }

      SetFanSpeedHelper(std::move(set_fan_rpm));
      stage_ = Stage::kVerifyIncrease;
      verify_count_ = 0;
      return;
    }
    case Stage::kVerifyIncrease: {
      SetPercentage(20 + (10 * verify_count_));

      // Move any "passed" fans into the `passed_fan_ids` array.
      for (int id = 0; id < original_fan_speed_.size(); ++id) {
        if (remaining_fan_ids_.contains(id) &&
            fan_speed[id] >= original_fan_speed_[id] + kFanRpmDelta) {
          passed_fan_ids_.push_back(id);
          remaining_fan_ids_.erase(id);
        }
      }

      if (remaining_fan_ids_.size() == 0) {
        TerminateFanRoutine();
        return;
      }

      // If we've checked whether the fan speed has increased for three times,
      // we should attempt to check if it can be decreased.
      if (verify_count_ >= 2) {
        stage_ = Stage::kSetDecrease;
        HandleGetFanSpeed(fan_speed, error);
        return;
      }

      // Update the verify count value.
      verify_count_ += 1;

      DelayGetFanSpeed();
      return;
    }
    case Stage::kSetDecrease: {
      base::flat_map<uint8_t, uint16_t> set_fan_rpm = {};
      SetPercentage(60);

      for (const auto& id : remaining_fan_ids_) {
        set_fan_rpm.insert(
            {id, std::max(static_cast<int32_t>(original_fan_speed_[id]) -
                              kFanRpmChange,
                          0)});
      }

      SetFanSpeedHelper(set_fan_rpm);
      stage_ = Stage::kVerifyDecrease;
      verify_count_ = 0;
      return;
    }
    case Stage::kVerifyDecrease: {
      SetPercentage(70 + (10 * verify_count_));

      // Move any "passed" fans into the `passed_fan_ids` array.
      for (int id = 0; id < original_fan_speed_.size(); ++id) {
        if (remaining_fan_ids_.contains(id) &&
            original_fan_speed_[id] >= fan_speed[id] + kFanRpmDelta) {
          passed_fan_ids_.push_back(id);
          remaining_fan_ids_.erase(id);
        }
      }

      // We've checked whether the fan speed has decreased for three times.
      // We should now terminate the routine and return the routine result.
      if (verify_count_ >= 2 || remaining_fan_ids_.size() == 0) {
        TerminateFanRoutine();
        return;
      }

      verify_count_ += 1;

      DelayGetFanSpeed();
      return;
    }
  }
}

void FanRoutine::SetFanSpeedHelper(
    base::flat_map<uint8_t, uint16_t> set_fan_rpms) {
  if (set_fan_rpms.size() == 0) {
    // There is no fan to be set. We can attempt to terminate the routine.
    TerminateFanRoutine();
    return;
  }

  context_->executor()->SetFanSpeed(
      set_fan_rpms, base::BindOnce(&FanRoutine::HandleSetFanSpeed,
                                   weak_ptr_factory_.GetWeakPtr()));
}

void FanRoutine::HandleSetFanSpeed(const std::optional<std::string>& error) {
  if (error.has_value()) {
    ReleaseResources();
    RaiseException(error.value());
    return;
  }

  DelayGetFanSpeed();
}

mojom::HardwarePresenceStatus FanRoutine::CheckFanCount() {
  auto actual_fan_count = original_fan_speed_.size();

  return actual_fan_count == expected_fan_count_
             ? mojom::HardwarePresenceStatus::kMatched
             : mojom::HardwarePresenceStatus::kNotMatched;
}

void FanRoutine::TerminateFanRoutine() {
  ReleaseResources();
  mojom::HardwarePresenceStatus fan_count_status = CheckFanCount();
  bool passed =
      remaining_fan_ids_.size() == 0 &&
      (fan_count_status == mojom::HardwarePresenceStatus::kNotConfigured ||
       fan_count_status == mojom::HardwarePresenceStatus::kMatched);
  auto failed_fan_ids = std::vector<uint8_t>(remaining_fan_ids_.begin(),
                                             remaining_fan_ids_.end());
  auto fan_routine_detail =
      mojom::RoutineDetail::NewFan(mojom::FanRoutineDetail::New(
          passed_fan_ids_, failed_fan_ids, fan_count_status));
  SetFinishedState(passed, std::move(fan_routine_detail));
}

void FanRoutine::ReleaseResources() {
  if (notify_resource_queue_finished_) {
    std::move(notify_resource_queue_finished_).RunAndReset();
  }
  if (reset_fan_control_) {
    std::move(reset_fan_control_).RunAndReset();
  }
}

}  // namespace diagnostics
