// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/battery_discharge/battery_discharge.h"

#include <inttypes.h>

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include <base/check.h>
#include <base/check_op.h>
#include <base/json/json_writer.h>
#include <base/logging.h>
#include <base/task/single_thread_task_runner.h>

#include "diagnostics/base/mojo_utils.h"
#include "diagnostics/cros_healthd/routines/battery_discharge/battery_discharge_constants.h"
#include "diagnostics/mojom/public/cros_healthd_diagnostics.mojom.h"

namespace diagnostics {

namespace mojo_ipc = ::ash::cros_healthd::mojom;

BatteryDischargeRoutine::BatteryDischargeRoutine(
    Context* const context,
    base::TimeDelta exec_duration,
    uint32_t maximum_discharge_percent_allowed,
    const base::TickClock* tick_clock)
    : context_(context),
      exec_duration_(exec_duration),
      maximum_discharge_percent_allowed_(maximum_discharge_percent_allowed) {
  if (tick_clock) {
    tick_clock_ = tick_clock;
  } else {
    default_tick_clock_ = std::make_unique<base::DefaultTickClock>();
    tick_clock_ = default_tick_clock_.get();
  }
  DCHECK(context_);
  DCHECK(tick_clock_);
}

BatteryDischargeRoutine::~BatteryDischargeRoutine() = default;

void BatteryDischargeRoutine::Start() {
  DCHECK_EQ(GetStatus(), mojo_ipc::DiagnosticRoutineStatusEnum::kReady);
  // Transition to waiting so the user can unplug the charger if necessary.
  UpdateStatus(mojo_ipc::DiagnosticRoutineStatusEnum::kWaiting, "");
  CalculateProgressPercent();
}

void BatteryDischargeRoutine::Resume() {
  DCHECK_EQ(GetStatus(), mojo_ipc::DiagnosticRoutineStatusEnum::kWaiting);
  RunBatteryDischargeRoutine();
  if (GetStatus() != mojo_ipc::DiagnosticRoutineStatusEnum::kRunning)
    LOG(ERROR) << "Routine failed: " << GetStatusMessage();
}

void BatteryDischargeRoutine::Cancel() {
  auto status = GetStatus();
  // Cancel the routine if it hasn't already finished.
  if (status == mojo_ipc::DiagnosticRoutineStatusEnum::kPassed ||
      status == mojo_ipc::DiagnosticRoutineStatusEnum::kFailed ||
      status == mojo_ipc::DiagnosticRoutineStatusEnum::kError) {
    return;
  }

  CalculateProgressPercent();

  callback_.Cancel();
  UpdateStatus(mojo_ipc::DiagnosticRoutineStatusEnum::kCancelled,
               kBatteryDischargeRoutineCancelledMessage);
}

void BatteryDischargeRoutine::PopulateStatusUpdate(
    mojo_ipc::RoutineUpdate* response, bool include_output) {
  auto status = GetStatus();
  if (status == mojo_ipc::DiagnosticRoutineStatusEnum::kWaiting) {
    auto interactive_update = mojo_ipc::InteractiveRoutineUpdate::New();
    interactive_update->user_message =
        mojo_ipc::DiagnosticRoutineUserMessageEnum::kUnplugACPower;
    response->routine_update_union =
        mojo_ipc::RoutineUpdateUnion::NewInteractiveUpdate(
            std::move(interactive_update));
  } else {
    auto noninteractive_update = mojo_ipc::NonInteractiveRoutineUpdate::New();
    noninteractive_update->status = status;
    noninteractive_update->status_message = GetStatusMessage();

    response->routine_update_union =
        mojo_ipc::RoutineUpdateUnion::NewNoninteractiveUpdate(
            std::move(noninteractive_update));
  }

  CalculateProgressPercent();
  response->progress_percent = progress_percent_;
  if (include_output && !output_dict_.empty()) {
    std::string json;
    base::JSONWriter::Write(output_dict_, &json);
    response->output =
        CreateReadOnlySharedMemoryRegionMojoHandle(base::StringPiece(json));
  }
}

void BatteryDischargeRoutine::CalculateProgressPercent() {
  auto status = GetStatus();
  if (status == mojo_ipc::DiagnosticRoutineStatusEnum::kPassed ||
      status == mojo_ipc::DiagnosticRoutineStatusEnum::kFailed) {
    // The routine has finished, so report 100.
    progress_percent_ = 100;
  } else if (status != mojo_ipc::DiagnosticRoutineStatusEnum::kError &&
             status != mojo_ipc::DiagnosticRoutineStatusEnum::kCancelled &&
             start_ticks_.has_value()) {
    progress_percent_ =
        100 * (tick_clock_->NowTicks() - start_ticks_.value()) / exec_duration_;
  }
}

void BatteryDischargeRoutine::RunBatteryDischargeRoutine() {
  if (maximum_discharge_percent_allowed_ > 100) {
    UpdateStatus(mojo_ipc::DiagnosticRoutineStatusEnum::kError,
                 kBatteryDischargeRoutineInvalidParametersMessage);
    return;
  }

  std::optional<power_manager::PowerSupplyProperties> response =
      context_->powerd_adapter()->GetPowerSupplyProperties();
  if (!response.has_value()) {
    UpdateStatus(mojo_ipc::DiagnosticRoutineStatusEnum::kError,
                 kPowerdPowerSupplyPropertiesFailedMessage);
    return;
  }
  auto power_supply_proto = response.value();

  if (power_supply_proto.battery_state() !=
      power_manager::PowerSupplyProperties_BatteryState_DISCHARGING) {
    UpdateStatus(mojo_ipc::DiagnosticRoutineStatusEnum::kError,
                 kBatteryDischargeRoutineNotDischargingMessage);
    return;
  }

  double beginning_charge_percent = power_supply_proto.battery_percent();

  start_ticks_ = tick_clock_->NowTicks();

  callback_.Reset(
      base::BindOnce(&BatteryDischargeRoutine::DetermineRoutineResult,
                     weak_ptr_factory_.GetWeakPtr(), beginning_charge_percent));
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, callback_.callback(), exec_duration_);

  UpdateStatus(mojo_ipc::DiagnosticRoutineStatusEnum::kRunning,
               kBatteryDischargeRoutineRunningMessage);
}

void BatteryDischargeRoutine::DetermineRoutineResult(
    double beginning_charge_percent) {
  std::optional<power_manager::PowerSupplyProperties> response =
      context_->powerd_adapter()->GetPowerSupplyProperties();
  if (!response.has_value()) {
    UpdateStatus(mojo_ipc::DiagnosticRoutineStatusEnum::kError,
                 kPowerdPowerSupplyPropertiesFailedMessage);
    LOG(ERROR) << kPowerdPowerSupplyPropertiesFailedMessage;
    return;
  }
  auto power_supply_proto = response.value();
  double ending_charge_percent = power_supply_proto.battery_percent();

  if (beginning_charge_percent < ending_charge_percent) {
    UpdateStatus(mojo_ipc::DiagnosticRoutineStatusEnum::kError,
                 kBatteryDischargeRoutineNotDischargingMessage);
    LOG(ERROR) << kBatteryDischargeRoutineNotDischargingMessage;
    return;
  }

  double discharge_percent = beginning_charge_percent - ending_charge_percent;
  base::Value::Dict result_dict;
  result_dict.Set("dischargePercent", discharge_percent);
  output_dict_.Set("resultDetails", std::move(result_dict));
  if (discharge_percent > maximum_discharge_percent_allowed_) {
    UpdateStatus(mojo_ipc::DiagnosticRoutineStatusEnum::kFailed,
                 kBatteryDischargeRoutineFailedExcessiveDischargeMessage);
    return;
  }

  UpdateStatus(mojo_ipc::DiagnosticRoutineStatusEnum::kPassed,
               kBatteryDischargeRoutineSucceededMessage);
}

}  // namespace diagnostics
