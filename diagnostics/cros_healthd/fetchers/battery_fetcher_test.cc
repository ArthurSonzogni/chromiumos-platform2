// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fetchers/battery_fetcher.h"

#include <base/files/file_util.h>
#include <base/test/gmock_callback_support.h>
#include <base/test/test_future.h>
#include <brillo/errors/error.h>
#include <brillo/files/file_util.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <power_manager/proto_bindings/power_supply_properties.pb.h>

#include "diagnostics/base/file_test_utils.h"
#include "diagnostics/base/paths.h"
#include "diagnostics/cros_healthd/executor/mock_executor.h"
#include "diagnostics/cros_healthd/system/fake_powerd_adapter.h"
#include "diagnostics/cros_healthd/system/fake_system_config.h"
#include "diagnostics/cros_healthd/system/mock_context.h"

namespace diagnostics {
namespace {

namespace mojom = ::ash::cros_healthd::mojom;

using ::testing::_;
using ::testing::DoAll;
using ::testing::Return;
using ::testing::WithArg;

// Arbitrary test values for the various battery metrics.
constexpr power_manager::PowerSupplyProperties_BatteryState kBatteryStateFull =
    power_manager::PowerSupplyProperties_BatteryState_FULL;
constexpr char kBatteryVendor[] = "TEST_MFR";
constexpr double kBatteryVoltage = 127.45;
constexpr int kBatteryCycleCount = 2;
constexpr char kBatterySerialNumber[] = "1000";
constexpr double kBatteryVoltageMinDesign = 114.00;
constexpr double kBatteryChargeFull = 4.3;
constexpr double kBatteryChargeFullDesign = 3.92;
constexpr char kBatteryModelName[] = "TEST_MODEL_NAME";
constexpr double kBatteryChargeNow = 5.17;
constexpr uint32_t kSmartBatteryManufactureDate = 0x4d06;
constexpr char kSmartBatteryManufactureDateString[] = "2018-08-06";
constexpr uint32_t kSmartBatteryTemperature = 0xbae;
constexpr double kBatteryCurrentNow = 6.45;
constexpr char kBatteryTechnology[] = "Battery technology.";
constexpr char kBatteryStatus[] = "Discharging";

// Device model name and corresponding i2c port.
constexpr char kModelName[] = "drobit";
constexpr uint8_t kI2CPort = 5;

class BatteryFetcherTest : public BaseFileTest {
 protected:
  BatteryFetcherTest() = default;

  void SetUp() override {
    mock_context_.fake_system_config()->SetHasBattery(true);
    mock_context_.fake_system_config()->SetHasSmartBattery(true);
    mock_context_.fake_system_config()->SetCodeName(kModelName);
    SetFile(paths::sysfs::kCrosEc, "");
  }

  mojom::BatteryResultPtr FetchBatteryInfoSync() {
    base::test::TestFuture<mojom::BatteryResultPtr> future;
    FetchBatteryInfo(&mock_context_, future.GetCallback());
    return future.Take();
  }

  MockExecutor* mock_executor() { return mock_context_.mock_executor(); }

  FakePowerdAdapter* fake_powerd_adapter() {
    return mock_context_.fake_powerd_adapter();
  }

  MockContext mock_context_;
};

// Test that we can fetch all battery metrics correctly.
TEST_F(BatteryFetcherTest, FetchBatteryInfo) {
  // Create PowerSupplyProperties response protobuf.
  power_manager::PowerSupplyProperties power_supply_proto;
  power_supply_proto.set_battery_state(kBatteryStateFull);
  power_supply_proto.set_battery_vendor(kBatteryVendor);
  power_supply_proto.set_battery_voltage(kBatteryVoltage);
  power_supply_proto.set_battery_cycle_count(kBatteryCycleCount);
  power_supply_proto.set_battery_charge_full(kBatteryChargeFull);
  power_supply_proto.set_battery_charge_full_design(kBatteryChargeFullDesign);
  power_supply_proto.set_battery_serial_number(kBatterySerialNumber);
  power_supply_proto.set_battery_voltage_min_design(kBatteryVoltageMinDesign);
  power_supply_proto.set_battery_model_name(kBatteryModelName);
  power_supply_proto.set_battery_charge(kBatteryChargeNow);
  power_supply_proto.set_battery_current(kBatteryCurrentNow);
  power_supply_proto.set_battery_technology(kBatteryTechnology);
  power_supply_proto.set_battery_status(kBatteryStatus);

  fake_powerd_adapter()->SetPowerSupplyProperties(power_supply_proto);

  EXPECT_CALL(*mock_executor(), GetSmartBatteryManufactureDate(kI2CPort, _))
      .WillRepeatedly(
          base::test::RunOnceCallback<1>(kSmartBatteryManufactureDate));
  EXPECT_CALL(*mock_executor(), GetSmartBatteryTemperature(kI2CPort, _))
      .WillRepeatedly(base::test::RunOnceCallback<1>(kSmartBatteryTemperature));

  auto battery_result = FetchBatteryInfoSync();
  ASSERT_TRUE(battery_result->is_battery_info());

  const auto& battery = battery_result->get_battery_info();
  EXPECT_EQ(battery->cycle_count, kBatteryCycleCount);
  EXPECT_EQ(battery->vendor, kBatteryVendor);
  EXPECT_EQ(battery->voltage_now, kBatteryVoltage);
  EXPECT_EQ(battery->charge_full, kBatteryChargeFull);
  EXPECT_EQ(battery->charge_full_design, kBatteryChargeFullDesign);
  EXPECT_EQ(battery->serial_number, kBatterySerialNumber);
  EXPECT_EQ(battery->voltage_min_design, kBatteryVoltageMinDesign);
  EXPECT_EQ(battery->model_name, kBatteryModelName);
  EXPECT_EQ(battery->charge_now, kBatteryChargeNow);
  EXPECT_EQ(battery->current_now, kBatteryCurrentNow);
  EXPECT_EQ(battery->technology, kBatteryTechnology);
  EXPECT_EQ(battery->status, kBatteryStatus);

  // Test that optional smart battery metrics are populated.
  ASSERT_TRUE(battery->manufacture_date.has_value());
  ASSERT_TRUE(battery->temperature);
  EXPECT_EQ(battery->manufacture_date.value(),
            kSmartBatteryManufactureDateString);
  EXPECT_EQ(battery->temperature->value, kSmartBatteryTemperature);
}

// Test that an empty proto in a power_manager D-Bus response returns an error.
TEST_F(BatteryFetcherTest, EmptyProtoPowerManagerDbusResponse) {
  power_manager::PowerSupplyProperties power_supply_proto;
  fake_powerd_adapter()->SetPowerSupplyProperties(power_supply_proto);
  auto battery_result = FetchBatteryInfoSync();
  ASSERT_TRUE(battery_result->is_error());
  EXPECT_EQ(battery_result->get_error()->type,
            mojom::ErrorType::kSystemUtilityError);
}

// Test that executor failing collect non-null battery manufacture date returns
// an error.
TEST_F(BatteryFetcherTest, ManufactureDateRetrievalFailure) {
  power_manager::PowerSupplyProperties power_supply_proto;
  power_supply_proto.set_battery_state(kBatteryStateFull);
  fake_powerd_adapter()->SetPowerSupplyProperties(power_supply_proto);

  EXPECT_CALL(*mock_executor(), GetSmartBatteryManufactureDate(kI2CPort, _))
      .WillRepeatedly(base::test::RunOnceCallback<1>(std::nullopt));
  EXPECT_CALL(*mock_executor(), GetSmartBatteryTemperature(kI2CPort, _))
      .WillRepeatedly(base::test::RunOnceCallback<1>(kSmartBatteryTemperature));

  auto battery_result = FetchBatteryInfoSync();
  ASSERT_TRUE(battery_result->is_error());
  EXPECT_EQ(battery_result->get_error()->type,
            mojom::ErrorType::kSystemUtilityError);
}

// Test that executor failing collect non-null battery temperature returns an
// error.
TEST_F(BatteryFetcherTest, TemperatureRetrievalFailure) {
  power_manager::PowerSupplyProperties power_supply_proto;
  power_supply_proto.set_battery_state(kBatteryStateFull);
  fake_powerd_adapter()->SetPowerSupplyProperties(power_supply_proto);

  EXPECT_CALL(*mock_executor(), GetSmartBatteryManufactureDate(kI2CPort, _))
      .WillRepeatedly(
          base::test::RunOnceCallback<1>(kSmartBatteryManufactureDate));
  EXPECT_CALL(*mock_executor(), GetSmartBatteryTemperature(kI2CPort, _))
      .WillRepeatedly(base::test::RunOnceCallback<1>(std::nullopt));

  auto battery_result = FetchBatteryInfoSync();
  ASSERT_TRUE(battery_result->is_error());
  EXPECT_EQ(battery_result->get_error()->type,
            mojom::ErrorType::kSystemUtilityError);
}

// Test if we can handle the error when EC is not found on a device with smart
// battery.
TEST_F(BatteryFetcherTest, EcNotFoundSmartBatteryDevice) {
  power_manager::PowerSupplyProperties power_supply_proto;
  power_supply_proto.set_battery_state(kBatteryStateFull);
  fake_powerd_adapter()->SetPowerSupplyProperties(power_supply_proto);

  UnsetPath(paths::sysfs::kCrosEc);
  // Set the wrong config.
  mock_context_.fake_system_config()->SetHasSmartBattery(true);

  auto battery_result = FetchBatteryInfoSync();
  ASSERT_TRUE(battery_result->is_error());
  EXPECT_EQ(battery_result->get_error()->type,
            mojom::ErrorType::kSystemUtilityError);
}

// Test if we can handle the error when the device model is not a device with
// smart battery.
TEST_F(BatteryFetcherTest, NoSmartBatteryDeviceModel) {
  power_manager::PowerSupplyProperties power_supply_proto;
  power_supply_proto.set_battery_state(kBatteryStateFull);
  fake_powerd_adapter()->SetPowerSupplyProperties(power_supply_proto);

  // Set the wrong config.
  mock_context_.fake_system_config()->SetHasSmartBattery(true);
  mock_context_.fake_system_config()->SetCodeName("NO_SMART_BATTERY_MODEL");

  auto battery_result = FetchBatteryInfoSync();
  ASSERT_TRUE(battery_result->is_error());
  EXPECT_EQ(battery_result->get_error()->type,
            mojom::ErrorType::kSystemUtilityError);
}

// Test that Smart Battery metrics are not fetched when a device does not have a
// Smart Battery.
TEST_F(BatteryFetcherTest, NoSmartBattery) {
  mock_context_.fake_system_config()->SetHasSmartBattery(false);

  // Set the mock power manager response.
  power_manager::PowerSupplyProperties power_supply_proto;
  power_supply_proto.set_battery_state(kBatteryStateFull);
  fake_powerd_adapter()->SetPowerSupplyProperties(power_supply_proto);

  auto battery_result = FetchBatteryInfoSync();
  ASSERT_TRUE(battery_result->is_battery_info());
  const auto& battery = battery_result->get_battery_info();

  EXPECT_FALSE(battery->manufacture_date.has_value());
  EXPECT_FALSE(battery->temperature);
}

// Test that no battery info is returned when a device does not have a battery.
TEST_F(BatteryFetcherTest, NoBattery) {
  mock_context_.fake_system_config()->SetHasBattery(false);
  auto battery_result = FetchBatteryInfoSync();
  ASSERT_TRUE(battery_result->get_battery_info().is_null());
}

}  // namespace
}  // namespace diagnostics
