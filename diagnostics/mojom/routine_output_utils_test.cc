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
  detail->peripherals.push_back(std::move(peripheral1));

  auto peripheral2 = mojom::BluetoothScannedPeripheralInfo::New();
  peripheral2->rssi_history = std::vector<int16_t>{-100, -90, -80};
  peripheral2->name = std::nullopt;
  peripheral2->peripheral_id = std::nullopt;
  detail->peripherals.push_back(std::move(peripheral2));

  base::Value::Dict expected_peripheral1;
  base::Value::List expected_rssi_history1;
  expected_rssi_history1.Append(-40);
  expected_rssi_history1.Append(-50);
  expected_rssi_history1.Append(-60);
  expected_peripheral1.Set("rssi_history", std::move(expected_rssi_history1));
  expected_peripheral1.Set("name", "TEST_PERIPHERAL_1");
  expected_peripheral1.Set("peripheral_id", "TEST_ID_1");
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

}  // namespace
}  // namespace diagnostics
