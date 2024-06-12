// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/mojom/routine_output_utils.h"

#include <string>
#include <utility>

#include <base/values.h>
#include <base/strings/string_number_conversions.h>

#include "diagnostics/mojom/public/cros_healthd_routines.mojom.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

std::string EnumToString(mojom::HardwarePresenceStatus state) {
  switch (state) {
    case mojom::HardwarePresenceStatus::kUnmappedEnumField:
      NOTREACHED_NORETURN();
    case mojom::HardwarePresenceStatus::kMatched:
      return "Matched";
    case mojom::HardwarePresenceStatus::kNotMatched:
      return "Not Matched";
    case mojom::HardwarePresenceStatus::kNotConfigured:
      return "Not Configured";
  }
}

std::string EnumToStringInSensitiveSensorV1Format(
    mojom::HardwarePresenceStatus state) {
  switch (state) {
    case mojom::HardwarePresenceStatus::kUnmappedEnumField:
      NOTREACHED_NORETURN();
    case mojom::HardwarePresenceStatus::kMatched:
      return "passed";
    case mojom::HardwarePresenceStatus::kNotMatched:
      return "unexpected";
    case mojom::HardwarePresenceStatus::kNotConfigured:
      return "skipped";
  }
}

std::string EnumToString(
    mojom::BluetoothPairingPeripheralInfo_PairError error) {
  switch (error) {
    case mojom::BluetoothPairingPeripheralInfo_PairError::kUnmappedEnumField:
      NOTREACHED_NORETURN();
    case mojom::BluetoothPairingPeripheralInfo_PairError::kNone:
      return "None";
    case mojom::BluetoothPairingPeripheralInfo_PairError::kBondFailed:
      return "Bond Failed";
    case mojom::BluetoothPairingPeripheralInfo_PairError::kBadStatus:
      return "Bad Status";
    case mojom::BluetoothPairingPeripheralInfo_PairError::kSspFailed:
      return "Ssp Failed";
    case mojom::BluetoothPairingPeripheralInfo_PairError::kTimeout:
      return "Timeout";
  }
}

std::string EnumToString(
    mojom::BluetoothPairingPeripheralInfo_ConnectError error) {
  switch (error) {
    case mojom::BluetoothPairingPeripheralInfo_ConnectError::kUnmappedEnumField:
      NOTREACHED_NORETURN();
    case mojom::BluetoothPairingPeripheralInfo_ConnectError::kNone:
      return "None";
    case mojom::BluetoothPairingPeripheralInfo_ConnectError::kNoConnectedEvent:
      return "No Connected Event";
    case mojom::BluetoothPairingPeripheralInfo_ConnectError::kNotConnected:
      return "Not Connected";
  }
}

std::string EnumToString(
    mojom::BluetoothPairingPeripheralInfo_AddressType address_type) {
  switch (address_type) {
    case mojom::BluetoothPairingPeripheralInfo_AddressType::kUnmappedEnumField:
      NOTREACHED_NORETURN();
    case mojom::BluetoothPairingPeripheralInfo_AddressType::kUnknown:
      return "Unknown";
    case mojom::BluetoothPairingPeripheralInfo_AddressType::kPublic:
      return "Public";
    case mojom::BluetoothPairingPeripheralInfo_AddressType::kRandom:
      return "Random";
  }
}

std::string EnumToString(mojom::CameraSubtestResult subtest_result) {
  switch (subtest_result) {
    case mojom::CameraSubtestResult::kUnmappedEnumField:
      NOTREACHED_NORETURN();
    case mojom::CameraSubtestResult::kNotRun:
      return "Not Run";
    case mojom::CameraSubtestResult::kPassed:
      return "Passed";
    case mojom::CameraSubtestResult::kFailed:
      return "Failed";
  }
}

std::string EnumToString(mojom::SensitiveSensorInfo::Type type) {
  switch (type) {
    case mojom::SensitiveSensorInfo::Type::kUnmappedEnumField:
      NOTREACHED_NORETURN();
    case mojom::SensitiveSensorInfo::Type::kAccel:
      return "Accel";
    case mojom::SensitiveSensorInfo::Type::kGyro:
      return "Gyro";
    case mojom::SensitiveSensorInfo::Type::kMagn:
      return "Magn";
    case mojom::SensitiveSensorInfo::Type::kGravity:
      return "Gravity";
  }
}

std::string EnumToString(mojom::CameraFrameAnalysisRoutineDetail::Issue issue) {
  switch (issue) {
    case mojom::CameraFrameAnalysisRoutineDetail::Issue::kUnmappedEnumField:
      NOTREACHED_NORETURN();
    case mojom::CameraFrameAnalysisRoutineDetail::Issue::kNone:
      return "None";
    case mojom::CameraFrameAnalysisRoutineDetail::Issue::
        kCameraServiceNotAvailable:
      return "Camera Service Not Available";
    case mojom::CameraFrameAnalysisRoutineDetail::Issue::
        kBlockedByPrivacyShutter:
      return "Blocked By Privacy Shutter";
    case mojom::CameraFrameAnalysisRoutineDetail::Issue::kLensAreDirty:
      return "Lens Are Dirty";
  }
}

base::Value::Dict ConvertToValue(const mojom::SensitiveSensorInfoPtr& info) {
  base::Value::Dict output;
  output.Set("id", info->id);
  base::Value::List out_types;
  for (const auto& type : info->types) {
    out_types.Append(EnumToString(type));
  }
  output.Set("types", std::move(out_types));

  base::Value::List out_channels;
  for (const auto& channel : info->channels) {
    out_channels.Append(channel);
  }
  output.Set("channels", std::move(out_channels));
  return output;
}

base::Value::Dict ConvertToValue(const mojom::SensitiveSensorReportPtr& report,
                                 bool v2_output = true) {
  base::Value::Dict output;

  base::Value::List out_passed_sensors;
  for (const auto& sensor : report->passed_sensors) {
    out_passed_sensors.Append(ConvertToValue(sensor));
  }
  output.Set("passed_sensors", std::move(out_passed_sensors));

  base::Value::List out_failed_sensors;
  for (const auto& sensor : report->failed_sensors) {
    out_failed_sensors.Append(ConvertToValue(sensor));
  }
  output.Set("failed_sensors", std::move(out_failed_sensors));

  if (v2_output) {
    output.Set("sensor_presence_status",
               EnumToString(report->sensor_presence_status));
  } else {
    output.Set("existence_check_result", EnumToStringInSensitiveSensorV1Format(
                                             report->sensor_presence_status));
  }
  return output;
}

}  // namespace

base::Value::Dict ConvertToValue(
    const mojom::AudioDriverRoutineDetailPtr& detail) {
  base::Value::Dict output;

  output.Set("internal_card_detected", detail->internal_card_detected);
  output.Set("audio_devices_succeed_to_open",
             detail->audio_devices_succeed_to_open);

  return output;
}

base::Value::Dict ConvertToValue(
    const mojom::BluetoothDiscoveryRoutineDetailPtr& detail) {
  base::Value::Dict output;

  if (detail->start_discovery_result) {
    base::Value::Dict start_discovery_result;
    start_discovery_result.Set("hci_discovering",
                               detail->start_discovery_result->hci_discovering);
    start_discovery_result.Set(
        "dbus_discovering", detail->start_discovery_result->dbus_discovering);
    output.Set("start_discovery_result", std::move(start_discovery_result));
  }

  if (detail->stop_discovery_result) {
    base::Value::Dict stop_discovery_result;
    stop_discovery_result.Set("hci_discovering",
                              detail->stop_discovery_result->hci_discovering);
    stop_discovery_result.Set("dbus_discovering",
                              detail->stop_discovery_result->dbus_discovering);
    output.Set("stop_discovery_result", std::move(stop_discovery_result));
  }

  return output;
}

base::Value::Dict ConvertToValue(
    const mojom::BluetoothPairingRoutineDetailPtr& detail) {
  base::Value::Dict output;

  if (detail->pairing_peripheral) {
    base::Value::Dict out_peripheral;
    const auto& peripheral = detail->pairing_peripheral;
    out_peripheral.Set("pair_error", EnumToString(peripheral->pair_error));
    out_peripheral.Set("connect_error",
                       EnumToString(peripheral->connect_error));

    base::Value::List out_uuids;
    for (const auto& uuid : peripheral->uuids) {
      out_uuids.Append(uuid.AsLowercaseString());
    }
    out_peripheral.Set("uuids", std::move(out_uuids));
    if (peripheral->bluetooth_class) {
      out_peripheral.Set(
          "bluetooth_class",
          base::NumberToString(peripheral->bluetooth_class.value()));
    }
    out_peripheral.Set("address_type", EnumToString(peripheral->address_type));

    out_peripheral.Set("is_address_valid", peripheral->is_address_valid);
    if (peripheral->failed_manufacturer_id) {
      out_peripheral.Set("failed_manufacturer_id",
                         peripheral->failed_manufacturer_id.value());
    }
    output.Set("pairing_peripheral", std::move(out_peripheral));
  }

  return output;
}

base::Value::Dict ConvertToValue(
    const mojom::BluetoothPowerRoutineDetailPtr& detail) {
  base::Value::Dict output;

  if (detail->power_off_result) {
    base::Value::Dict power_off_result;
    power_off_result.Set("hci_powered", detail->power_off_result->hci_powered);
    power_off_result.Set("dbus_powered",
                         detail->power_off_result->dbus_powered);
    output.Set("power_off_result", std::move(power_off_result));
  }

  if (detail->power_on_result) {
    base::Value::Dict power_on_result;
    power_on_result.Set("hci_powered", detail->power_on_result->hci_powered);
    power_on_result.Set("dbus_powered", detail->power_on_result->dbus_powered);
    output.Set("power_on_result", std::move(power_on_result));
  }

  return output;
}

base::Value::Dict ConvertToValue(
    const mojom::BluetoothScanningRoutineDetailPtr& detail) {
  base::Value::Dict output;
  base::Value::List out_peripherals;

  for (const auto& peripheral : detail->peripherals) {
    base::Value::Dict out_peripheral;
    base::Value::List out_rssi_history;
    for (const auto& rssi : peripheral->rssi_history) {
      out_rssi_history.Append(std::move(rssi));
    }
    out_peripheral.Set("rssi_history", std::move(out_rssi_history));
    if (peripheral->name.has_value()) {
      out_peripheral.Set("name", peripheral->name.value());
    }
    if (peripheral->peripheral_id.has_value()) {
      out_peripheral.Set("peripheral_id", peripheral->peripheral_id.value());
    }
    if (peripheral->uuids.has_value()) {
      base::Value::List out_uuids;
      for (const auto& uuid : peripheral->uuids.value()) {
        out_uuids.Append(uuid.AsLowercaseString());
      }
      out_peripheral.Set("uuids", std::move(out_uuids));
    }

    out_peripherals.Append(std::move(out_peripheral));
  }

  output.Set("peripherals", std::move(out_peripherals));
  return output;
}

base::Value::Dict ConvertToValue(
    const mojom::UfsLifetimeRoutineDetailPtr& detail) {
  base::Value::Dict output;

  output.Set("pre_eol_info", detail->pre_eol_info);
  output.Set("device_life_time_est_a", detail->device_life_time_est_a);
  output.Set("device_life_time_est_b", detail->device_life_time_est_b);

  return output;
}

base::Value::Dict ConvertToValue(const mojom::FanRoutineDetailPtr& detail) {
  base::Value::Dict output;
  base::Value::List passed_fan_ids;
  base::Value::List failed_fan_ids;

  for (const auto& fan_id : detail->passed_fan_ids) {
    passed_fan_ids.Append(fan_id);
  }
  for (const auto& fan_id : detail->failed_fan_ids) {
    failed_fan_ids.Append(fan_id);
  }

  output.Set("passed_fan_ids", std::move(passed_fan_ids));
  output.Set("failed_fan_ids", std::move(failed_fan_ids));
  output.Set("fan_count_status", EnumToString(detail->fan_count_status));

  return output;
}

base::Value::Dict ConvertToValue(
    const mojom::CameraAvailabilityRoutineDetailPtr& detail) {
  base::Value::Dict output;

  output.Set("camera_service_available_check",
             EnumToString(detail->camera_service_available_check));
  output.Set("camera_diagnostic_service_available_check",
             EnumToString(detail->camera_diagnostic_service_available_check));

  return output;
}

base::Value::Dict ConvertToValue(
    const mojom::NetworkBandwidthRoutineDetailPtr& detail) {
  base::Value::Dict output;

  output.Set("download_speed_kbps", detail->download_speed_kbps);
  output.Set("upload_speed_kbps", detail->upload_speed_kbps);

  return output;
}

base::Value::Dict ConvertToValue(
    const mojom::SensitiveSensorRoutineDetailPtr& detail) {
  base::Value::Dict output;

  output.Set("base_accelerometer", ConvertToValue(detail->base_accelerometer));
  output.Set("lid_accelerometer", ConvertToValue(detail->lid_accelerometer));
  output.Set("base_gyroscope", ConvertToValue(detail->base_gyroscope));
  output.Set("lid_gyroscope", ConvertToValue(detail->lid_gyroscope));
  output.Set("base_magnetometer", ConvertToValue(detail->base_magnetometer));
  output.Set("lid_magnetometer", ConvertToValue(detail->lid_magnetometer));
  output.Set("base_gravity_sensor",
             ConvertToValue(detail->base_gravity_sensor));
  output.Set("lid_gravity_sensor", ConvertToValue(detail->lid_gravity_sensor));

  return output;
}

base::Value::Dict ConvertToValueForV1(
    const mojom::SensitiveSensorRoutineDetailPtr& detail) {
  base::Value::Dict output;

  output.Set("base_accelerometer",
             ConvertToValue(detail->base_accelerometer, /*v2_output=*/false));
  output.Set("lid_accelerometer",
             ConvertToValue(detail->lid_accelerometer, /*v2_output=*/false));
  output.Set("base_gyroscope",
             ConvertToValue(detail->base_gyroscope, /*v2_output=*/false));
  output.Set("lid_gyroscope",
             ConvertToValue(detail->lid_gyroscope, /*v2_output=*/false));
  output.Set("base_magnetometer",
             ConvertToValue(detail->base_magnetometer, /*v2_output=*/false));
  output.Set("lid_magnetometer",
             ConvertToValue(detail->lid_magnetometer, /*v2_output=*/false));
  output.Set("base_gravity_sensor",
             ConvertToValue(detail->base_gravity_sensor, /*v2_output=*/false));
  output.Set("lid_gravity_sensor",
             ConvertToValue(detail->lid_gravity_sensor, /*v2_output=*/false));

  return output;
}

base::Value::Dict ConvertToValue(
    const mojom::CameraFrameAnalysisRoutineDetailPtr& detail) {
  base::Value::Dict output;

  output.Set("issue", EnumToString(detail->issue));
  output.Set("privacy_shutter_open_test",
             EnumToString(detail->privacy_shutter_open_test));
  output.Set("lens_not_dirty_test", EnumToString(detail->lens_not_dirty_test));

  return output;
}

base::Value::Dict ConvertToValue(
    const mojom::BatteryDischargeRoutineDetailPtr& detail) {
  base::Value::Dict output;

  output.Set("discharge_percent", detail->discharge_percent);

  return output;
}

}  // namespace diagnostics
