// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/battery_and_power/battery_discharge_v2.h"

#include <string>
#include <utility>

#include <base/task/single_thread_task_runner.h>
#include <power_manager/proto_bindings/power_supply_properties.pb.h>

#include "diagnostics/cros_healthd/routines/battery_and_power/battery_discharge_constants.h"
#include "diagnostics/cros_healthd/system/context.h"
#include "diagnostics/cros_healthd/system/ground_truth.h"
#include "diagnostics/cros_healthd/system/powerd_adapter.h"
#include "diagnostics/mojom/public/cros_healthd_routines.mojom.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

base::unexpected<mojom::SupportStatusPtr> MakeUnsupported(
    const std::string& message) {
  return base::unexpected(mojom::SupportStatus::NewUnsupported(
      mojom::Unsupported::New(message, /*reason=*/nullptr)));
}
}  // namespace

// static
base::expected<std::unique_ptr<BaseRoutineControl>, mojom::SupportStatusPtr>
BatteryDischargeRoutineV2::Create(
    Context* context,
    const ash::cros_healthd::mojom::BatteryDischargeRoutineArgumentPtr& arg) {
  CHECK(!arg.is_null());
  auto status = context->ground_truth()->PrepareRoutineBatteryDischarge();
  if (!status->is_supported()) {
    return base::unexpected(std::move(status));
  }

  if (arg->maximum_discharge_percent_allowed > 100) {
    return MakeUnsupported("Invalid maximum discharge percent allowed value");
  }

  if (arg->exec_duration <= base::Seconds(0)) {
    return MakeUnsupported(
        "Exec duration should not be less than or equal to zero seconds");
  }

  return base::ok(
      base::WrapUnique(new BatteryDischargeRoutineV2(context, arg)));
}

BatteryDischargeRoutineV2::BatteryDischargeRoutineV2(
    Context* const context,
    const ash::cros_healthd::mojom::BatteryDischargeRoutineArgumentPtr& arg)
    : context_(context),
      exec_duration_(arg->exec_duration),
      maximum_discharge_percent_allowed_(
          arg->maximum_discharge_percent_allowed) {
  CHECK(context_);
}

BatteryDischargeRoutineV2::~BatteryDischargeRoutineV2() = default;

void BatteryDischargeRoutineV2::OnStart() {
  SetWaitingInquiryState("Waiting for user to unplug the AC adapter.",
                         mojom::RoutineInquiry::NewUnplugAcAdapterInquiry(
                             mojom::UnplugAcAdapterInquiry::New()));
}

void BatteryDischargeRoutineV2::OnReplyInquiry(
    mojom::RoutineInquiryReplyPtr reply) {
  SetRunningState();

  std::optional<power_manager::PowerSupplyProperties> response =
      context_->powerd_adapter()->GetPowerSupplyProperties();

  if (!response.has_value()) {
    RaiseException(kPowerdPowerSupplyPropertiesFailedMessage);
    return;
  }
  auto power_supply_proto = response.value();

  if (power_supply_proto.has_battery_state() &&
      power_supply_proto.battery_state() !=
          power_manager::PowerSupplyProperties_BatteryState_DISCHARGING) {
    RaiseException(kBatteryDischargeRoutineNotDischargingMessage);
    return;
  }

  if (!power_supply_proto.has_battery_percent()) {
    RaiseException(kBatteryDischargeRoutineNoBatteryPercentMessage);
    return;
  }

  beginning_charge_percent_ = power_supply_proto.battery_percent();
  start_ticks_ = base::TimeTicks::Now();

  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindRepeating(&BatteryDischargeRoutineV2::Finish,
                          weak_ptr_factory_.GetWeakPtr()),
      exec_duration_);

  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&BatteryDischargeRoutineV2::UpdatePercentage,
                     weak_ptr_factory_.GetWeakPtr()),
      exec_duration_ / 100);
}

void BatteryDischargeRoutineV2::Finish() {
  std::optional<power_manager::PowerSupplyProperties> response =
      context_->powerd_adapter()->GetPowerSupplyProperties();
  if (!response.has_value()) {
    RaiseException(kPowerdPowerSupplyPropertiesFailedMessage);
    return;
  }
  auto power_supply_proto = response.value();

  if (!power_supply_proto.has_battery_percent()) {
    RaiseException(kBatteryDischargeRoutineNoBatteryPercentMessage);
    return;
  }
  double ending_charge_percent = power_supply_proto.battery_percent();

  if (beginning_charge_percent_ < ending_charge_percent) {
    RaiseException(kBatteryDischargeRoutineNotDischargingMessage);
    return;
  }

  double discharge_percent = beginning_charge_percent_ - ending_charge_percent;
  auto routine_detail = mojom::RoutineDetail::NewBatteryDischarge(
      mojom::BatteryDischargeRoutineDetail::New(discharge_percent));

  bool has_passed = discharge_percent <= maximum_discharge_percent_allowed_;
  SetFinishedState(has_passed, std::move(routine_detail));
}

void BatteryDischargeRoutineV2::UpdatePercentage() {
  uint8_t percentage = static_cast<uint8_t>(
      100.0 * (base::TimeTicks::Now() - start_ticks_) / exec_duration_);
  if (percentage > state()->percentage && percentage < 100) {
    SetPercentage(percentage);
  }

  if (state()->percentage < 99) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&BatteryDischargeRoutineV2::UpdatePercentage,
                       weak_ptr_factory_.GetWeakPtr()),
        exec_duration_ / 100);
  }
}

}  // namespace diagnostics
