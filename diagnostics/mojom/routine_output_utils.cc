// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/mojom/routine_output_utils.h"

#include <string>
#include <utility>

#include <base/values.h>

#include "diagnostics/mojom/public/cros_healthd_routines.mojom.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

std::string EnumToString(mojom::HardwarePresenceStatus state) {
  switch (state) {
    case mojom::HardwarePresenceStatus::kUnmappedEnumField:
      return "Unmapped enum field";
    case mojom::HardwarePresenceStatus::kMatched:
      return "Matched";
    case mojom::HardwarePresenceStatus::kNotMatched:
      return "Not Matched";
    case mojom::HardwarePresenceStatus::kNotConfigured:
      return "Not Configured";
  }
}

}  // namespace

base::Value::Dict ParseAudioDriverDetail(
    const mojom::AudioDriverRoutineDetailPtr& audio_driver_detail) {
  base::Value::Dict output;

  output.Set("internal_card_detected",
             audio_driver_detail->internal_card_detected);
  output.Set("audio_devices_succeed_to_open",
             audio_driver_detail->audio_devices_succeed_to_open);

  return output;
}

base::Value::Dict ParseBluetoothDiscoveryDetail(
    const mojom::BluetoothDiscoveryRoutineDetailPtr&
        bluetooth_discovery_detail) {
  base::Value::Dict output;

  if (bluetooth_discovery_detail->start_discovery_result) {
    base::Value::Dict start_discovery_result;
    start_discovery_result.Set(
        "hci_discovering",
        bluetooth_discovery_detail->start_discovery_result->hci_discovering);
    start_discovery_result.Set(
        "dbus_discovering",
        bluetooth_discovery_detail->start_discovery_result->dbus_discovering);
    output.Set("start_discovery_result", std::move(start_discovery_result));
  }

  if (bluetooth_discovery_detail->stop_discovery_result) {
    base::Value::Dict stop_discovery_result;
    stop_discovery_result.Set(
        "hci_discovering",
        bluetooth_discovery_detail->stop_discovery_result->hci_discovering);
    stop_discovery_result.Set(
        "dbus_discovering",
        bluetooth_discovery_detail->stop_discovery_result->dbus_discovering);
    output.Set("stop_discovery_result", std::move(stop_discovery_result));
  }

  return output;
}

base::Value::Dict ParseBluetoothPowerDetail(
    const mojom::BluetoothPowerRoutineDetailPtr& bluetooth_power_detail) {
  base::Value::Dict output;

  if (bluetooth_power_detail->power_off_result) {
    base::Value::Dict power_off_result;
    power_off_result.Set("hci_powered",
                         bluetooth_power_detail->power_off_result->hci_powered);
    power_off_result.Set(
        "dbus_powered", bluetooth_power_detail->power_off_result->dbus_powered);
    output.Set("power_off_result", std::move(power_off_result));
  }

  if (bluetooth_power_detail->power_on_result) {
    base::Value::Dict power_on_result;
    power_on_result.Set("hci_powered",
                        bluetooth_power_detail->power_on_result->hci_powered);
    power_on_result.Set("dbus_powered",
                        bluetooth_power_detail->power_on_result->dbus_powered);
    output.Set("power_on_result", std::move(power_on_result));
  }

  return output;
}

base::Value::Dict ParseUfsLifetimeDetail(
    const mojom::UfsLifetimeRoutineDetailPtr& ufs_lifetime_detail) {
  base::Value::Dict output;

  output.Set("pre_eol_info", ufs_lifetime_detail->pre_eol_info);
  output.Set("device_life_time_est_a",
             ufs_lifetime_detail->device_life_time_est_a);
  output.Set("device_life_time_est_b",
             ufs_lifetime_detail->device_life_time_est_b);

  return output;
}

base::Value::Dict ParseFanDetail(const mojom::FanRoutineDetailPtr& fan_detail) {
  base::Value::Dict output;
  base::Value::List passed_fan_ids;
  base::Value::List failed_fan_ids;

  for (const auto& fan_id : fan_detail->passed_fan_ids) {
    passed_fan_ids.Append(fan_id);
  }
  for (const auto& fan_id : fan_detail->failed_fan_ids) {
    failed_fan_ids.Append(fan_id);
  }

  output.Set("passed_fan_ids", std::move(passed_fan_ids));
  output.Set("failed_fan_ids", std::move(failed_fan_ids));
  output.Set("fan_count_status", EnumToString(fan_detail->fan_count_status));

  return output;
}

}  // namespace diagnostics
