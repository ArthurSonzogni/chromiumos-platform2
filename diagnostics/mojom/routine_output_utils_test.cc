// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/mojom/routine_output_utils.h"

#include <utility>
#include <vector>

#include <base/uuid.h>
#include <gtest/gtest.h>

#include "diagnostics/mojom/public/cros_healthd_routines.mojom.h"

namespace diagnostics {
namespace {

namespace mojom = ::ash::cros_healthd::mojom;

TEST(RoutineOutputUtilsTest, ConvertAudioDriverDetail) {
  auto detail = mojom::AudioDriverRoutineDetail::New();
  detail->internal_card_detected = false;
  detail->audio_devices_succeed_to_open = true;

  base::Value::Dict expected_result;
  expected_result.Set("internal_card_detected", false);
  expected_result.Set("audio_devices_succeed_to_open", true);
  EXPECT_EQ(ConvertToValue(detail), expected_result);
}

TEST(RoutineOutputUtilsTest, ConvertBluetoothDiscoveryDetail) {
  auto detail = mojom::BluetoothDiscoveryRoutineDetail::New();
  detail->start_discovery_result = mojom::BluetoothDiscoveringDetail::New();
  detail->start_discovery_result->dbus_discovering = true;
  detail->start_discovery_result->hci_discovering = true;
  detail->stop_discovery_result = mojom::BluetoothDiscoveringDetail::New();
  detail->stop_discovery_result->dbus_discovering = true;
  detail->stop_discovery_result->hci_discovering = false;

  base::Value::Dict start_discovery_result;
  start_discovery_result.Set("dbus_discovering", true);
  start_discovery_result.Set("hci_discovering", true);
  base::Value::Dict stop_discovery_result;
  stop_discovery_result.Set("dbus_discovering", true);
  stop_discovery_result.Set("hci_discovering", false);

  base::Value::Dict expected_result;
  expected_result.Set("start_discovery_result",
                      std::move(start_discovery_result));
  expected_result.Set("stop_discovery_result",
                      std::move(stop_discovery_result));
  EXPECT_EQ(ConvertToValue(detail), expected_result);
}

TEST(RoutineOutputUtilsTest, ConvertBluetoothPairingDetail) {
  auto peripheral = mojom::BluetoothPairingPeripheralInfo::New();
  peripheral->pair_error =
      mojom::BluetoothPairingPeripheralInfo_PairError::kBadStatus;
  peripheral->connect_error =
      mojom::BluetoothPairingPeripheralInfo_ConnectError::kNone;
  peripheral->uuids = {
      base::Uuid::ParseLowercase("0000110a-0000-1000-8000-00805f9b34fb"),
      base::Uuid::ParseLowercase("0000110f-0000-1000-8000-00805f9b34fb")};
  peripheral->bluetooth_class = 123456;
  peripheral->address_type =
      mojom::BluetoothPairingPeripheralInfo_AddressType::kPublic;
  peripheral->is_address_valid = false;
  peripheral->failed_manufacturer_id = "test_id";

  auto detail = mojom::BluetoothPairingRoutineDetail::New();
  detail->pairing_peripheral = std::move(peripheral);

  base::Value::Dict expected_peripheral;
  expected_peripheral.Set("connect_error", "None");
  expected_peripheral.Set("pair_error", "Bad Status");
  base::Value::List expected_uuids;
  expected_uuids.Append("0000110a-0000-1000-8000-00805f9b34fb");
  expected_uuids.Append("0000110f-0000-1000-8000-00805f9b34fb");
  expected_peripheral.Set("uuids", std::move(expected_uuids));
  expected_peripheral.Set("bluetooth_class", "123456");
  expected_peripheral.Set("address_type", "Public");
  expected_peripheral.Set("is_address_valid", false);
  expected_peripheral.Set("failed_manufacturer_id", "test_id");

  base::Value::Dict expected_result;
  expected_result.Set("pairing_peripheral", std::move(expected_peripheral));
  EXPECT_EQ(ConvertToValue(detail), expected_result);
}

TEST(RoutineOutputUtilsTest, ConvertBluetoothPowerDetail) {
  auto detail = mojom::BluetoothPowerRoutineDetail::New();
  detail->power_off_result = mojom::BluetoothPoweredDetail::New();
  detail->power_off_result->dbus_powered = false;
  detail->power_off_result->hci_powered = false;
  detail->power_on_result = mojom::BluetoothPoweredDetail::New();
  detail->power_on_result->dbus_powered = true;
  detail->power_on_result->hci_powered = false;

  base::Value::Dict power_off_result;
  power_off_result.Set("dbus_powered", false);
  power_off_result.Set("hci_powered", false);
  base::Value::Dict power_on_result;
  power_on_result.Set("dbus_powered", true);
  power_on_result.Set("hci_powered", false);

  base::Value::Dict expected_result;
  expected_result.Set("power_off_result", std::move(power_off_result));
  expected_result.Set("power_on_result", std::move(power_on_result));
  EXPECT_EQ(ConvertToValue(detail), expected_result);
}

TEST(RoutineOutputUtilsTest, ConvertBluetoothScanningDetail) {
  auto detail = mojom::BluetoothScanningRoutineDetail::New();
  auto peripheral1 = mojom::BluetoothScannedPeripheralInfo::New();
  peripheral1->rssi_history = std::vector<int16_t>{-40, -50, -60};
  peripheral1->name = "TEST_PERIPHERAL_1";
  peripheral1->peripheral_id = "TEST_ID_1";
  peripheral1->uuids = {
      base::Uuid::ParseLowercase("0000110a-0000-1000-8000-00805f9b34fb"),
      base::Uuid::ParseLowercase("0000110f-0000-1000-8000-00805f9b34fb")};
  detail->peripherals.push_back(std::move(peripheral1));

  auto peripheral2 = mojom::BluetoothScannedPeripheralInfo::New();
  peripheral2->rssi_history = std::vector<int16_t>{-100, -90, -80};
  peripheral2->name = std::nullopt;
  peripheral2->peripheral_id = std::nullopt;
  peripheral2->uuids = std::nullopt;
  detail->peripherals.push_back(std::move(peripheral2));

  base::Value::Dict expected_peripheral1;
  base::Value::List expected_rssi_history1;
  expected_rssi_history1.Append(-40);
  expected_rssi_history1.Append(-50);
  expected_rssi_history1.Append(-60);
  expected_peripheral1.Set("rssi_history", std::move(expected_rssi_history1));
  expected_peripheral1.Set("name", "TEST_PERIPHERAL_1");
  expected_peripheral1.Set("peripheral_id", "TEST_ID_1");
  base::Value::List expected_uuids;
  expected_uuids.Append("0000110a-0000-1000-8000-00805f9b34fb");
  expected_uuids.Append("0000110f-0000-1000-8000-00805f9b34fb");
  expected_peripheral1.Set("uuids", std::move(expected_uuids));
  base::Value::Dict expected_peripheral2;
  base::Value::List expected_rssi_history2;
  expected_rssi_history2.Append(-100);
  expected_rssi_history2.Append(-90);
  expected_rssi_history2.Append(-80);
  expected_peripheral2.Set("rssi_history", std::move(expected_rssi_history2));
  base::Value::List expected_peripherals;
  expected_peripherals.Append(std::move(expected_peripheral1));
  expected_peripherals.Append(std::move(expected_peripheral2));

  base::Value::Dict expected_result;
  expected_result.Set("peripherals", std::move(expected_peripherals));
  EXPECT_EQ(ConvertToValue(detail), expected_result);
}

TEST(RoutineOutputUtilsTest, ConvertUfsLifetimeDetail) {
  auto detail = mojom::UfsLifetimeRoutineDetail::New();
  detail->pre_eol_info = 1;
  detail->device_life_time_est_a = 2;
  detail->device_life_time_est_b = 3;

  base::Value::Dict expected_result;
  expected_result.Set("pre_eol_info", 1);
  expected_result.Set("device_life_time_est_a", 2);
  expected_result.Set("device_life_time_est_b", 3);
  EXPECT_EQ(ConvertToValue(detail), expected_result);
}

TEST(RoutineOutputUtilsTest, ConvertFanDetail) {
  auto detail = mojom::FanRoutineDetail::New();
  detail->passed_fan_ids = {0, 2};
  detail->failed_fan_ids = {1, 3};
  detail->fan_count_status = mojom::HardwarePresenceStatus::kMatched;

  base::Value::Dict expected_result;
  expected_result.Set("passed_fan_ids",
                      base::Value::List().Append(0).Append(2));
  expected_result.Set("failed_fan_ids",
                      base::Value::List().Append(1).Append(3));
  expected_result.Set("fan_count_status", "Matched");
  EXPECT_EQ(ConvertToValue(detail), expected_result);
}

TEST(RoutineOutputUtilsTest, ConvertCameraAvailabilityDetail) {
  auto detail = mojom::CameraAvailabilityRoutineDetail::New();
  detail->camera_service_available_check = mojom::CameraSubtestResult::kPassed;
  detail->camera_diagnostic_service_available_check =
      mojom::CameraSubtestResult::kFailed;

  base::Value::Dict expected_result;
  expected_result.Set("camera_service_available_check", "Passed");
  expected_result.Set("camera_diagnostic_service_available_check", "Failed");
  EXPECT_EQ(ConvertToValue(detail), expected_result);
}

TEST(RoutineOutputUtilsTest, ConvertNetworkBandwidthDetail) {
  auto detail = mojom::NetworkBandwidthRoutineDetail::New();
  detail->download_speed_kbps = 300.0;
  detail->upload_speed_kbps = 100.0;

  base::Value::Dict expected_result;
  expected_result.Set("download_speed_kbps", 300.0);
  expected_result.Set("upload_speed_kbps", 100.0);
  EXPECT_EQ(ConvertToValue(detail), expected_result);
}

TEST(RoutineOutputUtilsTest, ConvertSensitiveSensorDetail) {
  auto detail = mojom::SensitiveSensorRoutineDetail::New();
  // Construct input.
  {
    auto default_sensor_report = mojom::SensitiveSensorReport::New();
    default_sensor_report->sensor_presence_status =
        mojom::HardwarePresenceStatus::kNotConfigured;

    // Create a passed sensor with types "accel" and "gyro" on location "base".
    auto base_accel_gyro = mojom::SensitiveSensorInfo::New();
    base_accel_gyro->id = 0;
    base_accel_gyro->types = {mojom::SensitiveSensorInfo::Type::kAccel,
                              mojom::SensitiveSensorInfo::Type::kGyro};
    base_accel_gyro->channels = {"timestamp", "accel_x",   "accel_y",
                                 "accel_z",   "anglvel_x", "anglvel_y",
                                 "anglvel_z"};

    auto base_accel_report = default_sensor_report->Clone();
    base_accel_report->passed_sensors.push_back(base_accel_gyro->Clone());
    base_accel_report->sensor_presence_status =
        mojom::HardwarePresenceStatus::kMatched;
    detail->base_accelerometer = std::move(base_accel_report);

    auto base_gyro_report = default_sensor_report->Clone();
    base_gyro_report->passed_sensors.push_back(std::move(base_accel_gyro));
    base_gyro_report->sensor_presence_status =
        mojom::HardwarePresenceStatus::kMatched;
    detail->base_gyroscope = std::move(base_gyro_report);

    // Create a failed sensor with type "magn" on location "lid".
    auto lid_magn = mojom::SensitiveSensorInfo::New();
    lid_magn->id = 1;
    lid_magn->types = {mojom::SensitiveSensorInfo::Type::kMagn};
    lid_magn->channels = {"timestamp", "magn_x", "magn_y", "magn_z"};

    auto lid_magn_report = default_sensor_report->Clone();
    lid_magn_report->failed_sensors.push_back(std::move(lid_magn));
    lid_magn_report->sensor_presence_status =
        mojom::HardwarePresenceStatus::kNotConfigured;
    detail->lid_magnetometer = std::move(lid_magn_report);

    // Create a failed sensor with type "gravity" on location "lid".
    auto lid_gravity = mojom::SensitiveSensorInfo::New();
    lid_gravity->id = 2;
    lid_gravity->types = {mojom::SensitiveSensorInfo::Type::kGravity};
    lid_gravity->channels = {"timestamp", "gravity_x", "gravity_y",
                             "gravity_z"};

    auto lid_gravity_report = default_sensor_report->Clone();
    lid_gravity_report->failed_sensors.push_back(std::move(lid_gravity));
    lid_gravity_report->sensor_presence_status =
        mojom::HardwarePresenceStatus::kNotMatched;
    detail->lid_gravity_sensor = std::move(lid_gravity_report);

    // Other sensor types are not present in this test.
    detail->lid_accelerometer = default_sensor_report->Clone();
    detail->lid_gyroscope = default_sensor_report->Clone();
    detail->base_magnetometer = default_sensor_report->Clone();
    detail->base_gravity_sensor = default_sensor_report->Clone();
  }

  base::Value::Dict expected_result;
  // Construct output.
  {
    base::Value::Dict expected_default_report;
    expected_default_report.Set("passed_sensors", base::Value::List());
    expected_default_report.Set("failed_sensors", base::Value::List());
    expected_default_report.Set("sensor_presence_status", "Not Configured");

    base::Value::Dict expected_base_accel_gyro;
    expected_base_accel_gyro.Set("id", 0);
    expected_base_accel_gyro.Set(
        "types", base::Value::List().Append("Accel").Append("Gyro"));
    expected_base_accel_gyro.Set("channels", base::Value::List()
                                                 .Append("timestamp")
                                                 .Append("accel_x")
                                                 .Append("accel_y")
                                                 .Append("accel_z")
                                                 .Append("anglvel_x")
                                                 .Append("anglvel_y")
                                                 .Append("anglvel_z"));

    auto expected_base_accel_report = expected_default_report.Clone();
    expected_base_accel_report.FindList("passed_sensors")
        ->Append(expected_base_accel_gyro.Clone());
    expected_base_accel_report.Set("sensor_presence_status", "Matched");
    expected_result.Set("base_accelerometer",
                        std::move(expected_base_accel_report));

    auto expected_base_gyro_report = expected_default_report.Clone();
    expected_base_gyro_report.FindList("passed_sensors")
        ->Append(std::move(expected_base_accel_gyro));
    expected_base_gyro_report.Set("sensor_presence_status", "Matched");
    expected_result.Set("base_gyroscope", std::move(expected_base_gyro_report));

    base::Value::Dict expected_lid_magn;
    expected_lid_magn.Set("id", 1);
    expected_lid_magn.Set("types", base::Value::List().Append("Magn"));
    expected_lid_magn.Set("channels", base::Value::List()
                                          .Append("timestamp")
                                          .Append("magn_x")
                                          .Append("magn_y")
                                          .Append("magn_z"));
    auto expected_lid_magn_report = expected_default_report.Clone();
    expected_lid_magn_report.FindList("failed_sensors")
        ->Append(std::move(expected_lid_magn));
    expected_lid_magn_report.Set("sensor_presence_status", "Not Configured");
    expected_result.Set("lid_magnetometer",
                        std::move(expected_lid_magn_report));

    base::Value::Dict expected_lid_gravity;
    expected_lid_gravity.Set("id", 2);
    expected_lid_gravity.Set("types", base::Value::List().Append("Gravity"));
    expected_lid_gravity.Set("channels", base::Value::List()
                                             .Append("timestamp")
                                             .Append("gravity_x")
                                             .Append("gravity_y")
                                             .Append("gravity_z"));
    auto expected_lid_gravity_report = expected_default_report.Clone();
    expected_lid_gravity_report.FindList("failed_sensors")
        ->Append(std::move(expected_lid_gravity));
    expected_lid_gravity_report.Set("sensor_presence_status", "Not Matched");
    expected_result.Set("lid_gravity_sensor",
                        std::move(expected_lid_gravity_report));

    // Other sensor types are not present in this test.
    expected_result.Set("lid_accelerometer", expected_default_report.Clone());
    expected_result.Set("lid_gyroscope", expected_default_report.Clone());
    expected_result.Set("base_magnetometer", expected_default_report.Clone());
    expected_result.Set("base_gravity_sensor", expected_default_report.Clone());
  }

  EXPECT_EQ(ConvertToValue(detail), expected_result);
}

TEST(RoutineOutputUtilsTest, ConvertSensitiveSensorDetailForV1) {
  auto detail = mojom::SensitiveSensorRoutineDetail::New();
  // Construct input.
  {
    auto default_sensor_report = mojom::SensitiveSensorReport::New();
    default_sensor_report->sensor_presence_status =
        mojom::HardwarePresenceStatus::kNotConfigured;

    // Create a passed sensor with types "accel" and "gyro" on location "base".
    auto base_accel_gyro = mojom::SensitiveSensorInfo::New();
    base_accel_gyro->id = 0;
    base_accel_gyro->types = {mojom::SensitiveSensorInfo::Type::kAccel,
                              mojom::SensitiveSensorInfo::Type::kGyro};
    base_accel_gyro->channels = {"timestamp", "accel_x",   "accel_y",
                                 "accel_z",   "anglvel_x", "anglvel_y",
                                 "anglvel_z"};

    auto base_accel_report = default_sensor_report->Clone();
    base_accel_report->passed_sensors.push_back(base_accel_gyro->Clone());
    base_accel_report->sensor_presence_status =
        mojom::HardwarePresenceStatus::kMatched;
    detail->base_accelerometer = std::move(base_accel_report);

    auto base_gyro_report = default_sensor_report->Clone();
    base_gyro_report->passed_sensors.push_back(std::move(base_accel_gyro));
    base_gyro_report->sensor_presence_status =
        mojom::HardwarePresenceStatus::kMatched;
    detail->base_gyroscope = std::move(base_gyro_report);

    // Create a failed sensor with type "magn" on location "lid".
    auto lid_magn = mojom::SensitiveSensorInfo::New();
    lid_magn->id = 1;
    lid_magn->types = {mojom::SensitiveSensorInfo::Type::kMagn};
    lid_magn->channels = {"timestamp", "magn_x", "magn_y", "magn_z"};

    auto lid_magn_report = default_sensor_report->Clone();
    lid_magn_report->failed_sensors.push_back(std::move(lid_magn));
    lid_magn_report->sensor_presence_status =
        mojom::HardwarePresenceStatus::kNotConfigured;
    detail->lid_magnetometer = std::move(lid_magn_report);

    // Create a failed sensor with type "gravity" on location "lid".
    auto lid_gravity = mojom::SensitiveSensorInfo::New();
    lid_gravity->id = 2;
    lid_gravity->types = {mojom::SensitiveSensorInfo::Type::kGravity};
    lid_gravity->channels = {"timestamp", "gravity_x", "gravity_y",
                             "gravity_z"};

    auto lid_gravity_report = default_sensor_report->Clone();
    lid_gravity_report->failed_sensors.push_back(std::move(lid_gravity));
    lid_gravity_report->sensor_presence_status =
        mojom::HardwarePresenceStatus::kNotMatched;
    detail->lid_gravity_sensor = std::move(lid_gravity_report);

    // Other sensor types are not present in this test.
    detail->lid_accelerometer = default_sensor_report->Clone();
    detail->lid_gyroscope = default_sensor_report->Clone();
    detail->base_magnetometer = default_sensor_report->Clone();
    detail->base_gravity_sensor = default_sensor_report->Clone();
  }

  base::Value::Dict expected_result;
  // Construct output.
  {
    base::Value::Dict expected_default_report;
    expected_default_report.Set("passed_sensors", base::Value::List());
    expected_default_report.Set("failed_sensors", base::Value::List());
    expected_default_report.Set("existence_check_result", "skipped");

    base::Value::Dict expected_base_accel_gyro;
    expected_base_accel_gyro.Set("id", 0);
    expected_base_accel_gyro.Set(
        "types", base::Value::List().Append("Accel").Append("Gyro"));
    expected_base_accel_gyro.Set("channels", base::Value::List()
                                                 .Append("timestamp")
                                                 .Append("accel_x")
                                                 .Append("accel_y")
                                                 .Append("accel_z")
                                                 .Append("anglvel_x")
                                                 .Append("anglvel_y")
                                                 .Append("anglvel_z"));

    auto expected_base_accel_report = expected_default_report.Clone();
    expected_base_accel_report.FindList("passed_sensors")
        ->Append(expected_base_accel_gyro.Clone());
    expected_base_accel_report.Set("existence_check_result", "passed");
    expected_result.Set("base_accelerometer",
                        std::move(expected_base_accel_report));

    auto expected_base_gyro_report = expected_default_report.Clone();
    expected_base_gyro_report.FindList("passed_sensors")
        ->Append(std::move(expected_base_accel_gyro));
    expected_base_gyro_report.Set("existence_check_result", "passed");
    expected_result.Set("base_gyroscope", std::move(expected_base_gyro_report));

    base::Value::Dict expected_lid_magn;
    expected_lid_magn.Set("id", 1);
    expected_lid_magn.Set("types", base::Value::List().Append("Magn"));
    expected_lid_magn.Set("channels", base::Value::List()
                                          .Append("timestamp")
                                          .Append("magn_x")
                                          .Append("magn_y")
                                          .Append("magn_z"));
    auto expected_lid_magn_report = expected_default_report.Clone();
    expected_lid_magn_report.FindList("failed_sensors")
        ->Append(std::move(expected_lid_magn));
    expected_lid_magn_report.Set("existence_check_result", "skipped");
    expected_result.Set("lid_magnetometer",
                        std::move(expected_lid_magn_report));

    base::Value::Dict expected_lid_gravity;
    expected_lid_gravity.Set("id", 2);
    expected_lid_gravity.Set("types", base::Value::List().Append("Gravity"));
    expected_lid_gravity.Set("channels", base::Value::List()
                                             .Append("timestamp")
                                             .Append("gravity_x")
                                             .Append("gravity_y")
                                             .Append("gravity_z"));
    auto expected_lid_gravity_report = expected_default_report.Clone();
    expected_lid_gravity_report.FindList("failed_sensors")
        ->Append(std::move(expected_lid_gravity));
    expected_lid_gravity_report.Set("existence_check_result", "unexpected");
    expected_result.Set("lid_gravity_sensor",
                        std::move(expected_lid_gravity_report));

    // Other sensor types are not present in this test.
    expected_result.Set("lid_accelerometer", expected_default_report.Clone());
    expected_result.Set("lid_gyroscope", expected_default_report.Clone());
    expected_result.Set("base_magnetometer", expected_default_report.Clone());
    expected_result.Set("base_gravity_sensor", expected_default_report.Clone());
  }

  EXPECT_EQ(ConvertToValueForV1(detail), expected_result);
}

TEST(RoutineOutputUtilsTest, ConvertCameraFrameAnalysisDetail) {
  auto detail = mojom::CameraFrameAnalysisRoutineDetail::New();
  detail->issue = mojom::CameraFrameAnalysisRoutineDetail::Issue::kNone;
  detail->privacy_shutter_open_test = mojom::CameraSubtestResult::kPassed;
  detail->lens_not_dirty_test = mojom::CameraSubtestResult::kNotRun;

  base::Value::Dict expected_result;
  expected_result.Set("issue", "None");
  expected_result.Set("privacy_shutter_open_test", "Passed");
  expected_result.Set("lens_not_dirty_test", "Not Run");
  EXPECT_EQ(ConvertToValue(detail), expected_result);
}

}  // namespace
}  // namespace diagnostics
