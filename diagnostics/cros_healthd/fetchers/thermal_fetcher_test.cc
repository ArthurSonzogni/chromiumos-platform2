// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fetchers/thermal_fetcher.h"

#include <stdlib.h>

#include <string>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/test/test_future.h>
#include <brillo/files/file_util.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "diagnostics/base/file_test_utils.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace diagnostics {
namespace {

namespace mojom = ::ash::cros_healthd::mojom;
using ::testing::_;
using ::testing::UnorderedElementsAreArray;

constexpr char kFirstThermalZoneDirectory[] = "sys/class/thermal/thermal_zone1";
constexpr char kFirstThermalZoneType[] = "type_1";
constexpr char kFirstThermalZoneTemp[] = "30000";
constexpr char kSecondThermalZoneDirectory[] =
    "sys/class/thermal/thermal_zone2";
constexpr char kSecondThermalZoneType[] = "type_2";
constexpr char kSecondThermalZoneTemp[] = "40000";
constexpr char kFirstEcSensorName[] = "ec_sensor_1";
constexpr double kFirstEcSensorTemp = 30.1;

constexpr char kEcFailureMessage[] = "Failed to read thermal value from fan EC";

MATCHER_P(MatchesThermalSensorInfo, ptr, "") {
  return arg->name == ptr.get()->name &&
         arg->temperature_celsius == ptr.get()->temperature_celsius &&
         arg->source == ptr.get()->source;
}

class ThermalFetcherTest : public BaseFileTest {
 public:
  ThermalFetcherTest(const ThermalFetcherTest&) = delete;
  ThermalFetcherTest& operator=(const ThermalFetcherTest&) = delete;

 protected:
  ThermalFetcherTest() = default;

  void SetUp() override {
    // Set up first thermal zone.
    SetFile({kFirstThermalZoneDirectory, kThermalZoneTypeFileName},
            kFirstThermalZoneType);
    SetFile({kFirstThermalZoneDirectory, kThermalZoneTempFileName},
            kFirstThermalZoneTemp);

    // Set up second thermal zone.
    SetFile({kSecondThermalZoneDirectory, kThermalZoneTypeFileName},
            kSecondThermalZoneType);
    SetFile({kSecondThermalZoneDirectory, kThermalZoneTempFileName},
            kSecondThermalZoneTemp);
  }

  void SetUpEcExecutorCall() {
    // Set up expect call for EC data.
    EXPECT_CALL(*mock_executor(), GetEcThermalSensors(_))
        .WillOnce([](mojom::Executor::GetEcThermalSensorsCallback callback) {
          std::vector<mojom::ThermalSensorInfoPtr> ec_thermal_response;
          ec_thermal_response.push_back(mojom::ThermalSensorInfo::New(
              kFirstEcSensorName, kFirstEcSensorTemp,
              mojom::ThermalSensorInfo::ThermalSensorSource::kEc));
          std::move(callback).Run(std::move(ec_thermal_response), std::nullopt);
        });
  }

  double TemperatureMillicelsiusToDoubleCelsius(std::string temperature) {
    double temperature_millicelsius;
    EXPECT_TRUE(base::StringToDouble(temperature, &temperature_millicelsius));
    return temperature_millicelsius / 1000;
  }

  MockExecutor* mock_executor() { return mock_context_.mock_executor(); }

  mojom::ThermalResultPtr FetchThermalInfoSync() {
    base::test::TestFuture<mojom::ThermalResultPtr> future;
    FetchThermalInfo(&mock_context_, future.GetCallback());
    return future.Take();
  }

 private:
  MockContext mock_context_;
};

// Test that ec and sysfs info can both be fetched correctly.
TEST_F(ThermalFetcherTest, TestFetchSuccess) {
  SetUpEcExecutorCall();
  auto result = FetchThermalInfoSync();
  ASSERT_TRUE(result->is_thermal_info());

  const auto& info = result->get_thermal_info();
  EXPECT_EQ(info->thermal_sensors.size(), 3);
  auto first_ec_response = mojom::ThermalSensorInfo::New(
      kFirstEcSensorName, kFirstEcSensorTemp,
      mojom::ThermalSensorInfo::ThermalSensorSource::kEc);
  auto first_sysfs_response = mojom::ThermalSensorInfo::New(
      kFirstThermalZoneType,
      TemperatureMillicelsiusToDoubleCelsius(kFirstThermalZoneTemp),
      mojom::ThermalSensorInfo::ThermalSensorSource::kSysFs);
  auto second_sysfs_response = mojom::ThermalSensorInfo::New(
      kSecondThermalZoneType,
      TemperatureMillicelsiusToDoubleCelsius(kSecondThermalZoneTemp),
      mojom::ThermalSensorInfo::ThermalSensorSource::kSysFs);
  EXPECT_THAT(
      info->thermal_sensors,
      UnorderedElementsAreArray(
          {MatchesThermalSensorInfo(std::cref(first_ec_response)),
           MatchesThermalSensorInfo(std::cref(first_sysfs_response)),
           MatchesThermalSensorInfo(std::cref(second_sysfs_response))}));
}

// Test that fetcher works with no sysfs info.
TEST_F(ThermalFetcherTest, TestNoSysfsFetchSuccess) {
  SetUpEcExecutorCall();

  UnsetPath(kFirstThermalZoneDirectory);
  UnsetPath(kSecondThermalZoneDirectory);

  auto result = FetchThermalInfoSync();
  ASSERT_TRUE(result->is_thermal_info());

  const auto& info = result->get_thermal_info();
  EXPECT_EQ(info->thermal_sensors.size(), 1);
  auto first_ec_response = mojom::ThermalSensorInfo::New(
      kFirstEcSensorName, kFirstEcSensorTemp,
      mojom::ThermalSensorInfo::ThermalSensorSource::kEc);
  EXPECT_THAT(info->thermal_sensors,
              UnorderedElementsAreArray(
                  {MatchesThermalSensorInfo(std::cref(first_ec_response))}));
}

// Test that if one of the sysfs thermal zone is invalid, the other can still be
// parsed.
TEST_F(ThermalFetcherTest, TestInvalidSysfsFetchSuccess) {
  SetUpEcExecutorCall();

  SetFile({kFirstThermalZoneDirectory, kThermalZoneTempFileName},
          "invalid_temperature");

  auto result = FetchThermalInfoSync();
  ASSERT_TRUE(result->is_thermal_info());

  const auto& info = result->get_thermal_info();
  EXPECT_EQ(info->thermal_sensors.size(), 2);
  auto first_ec_response = mojom::ThermalSensorInfo::New(
      kFirstEcSensorName, kFirstEcSensorTemp,
      mojom::ThermalSensorInfo::ThermalSensorSource::kEc);
  auto second_sysfs_response = mojom::ThermalSensorInfo::New(
      kSecondThermalZoneType,
      TemperatureMillicelsiusToDoubleCelsius(kSecondThermalZoneTemp),
      mojom::ThermalSensorInfo::ThermalSensorSource::kSysFs);
  EXPECT_THAT(
      info->thermal_sensors,
      UnorderedElementsAreArray(
          {MatchesThermalSensorInfo(std::cref(first_ec_response)),
           MatchesThermalSensorInfo(std::cref(second_sysfs_response))}));
}

// Test that fetcher works with no ec info.
TEST_F(ThermalFetcherTest, TestNoEcFetchSuccess) {
  EXPECT_CALL(*mock_executor(), GetEcThermalSensors(_))
      .WillOnce([](mojom::Executor::GetEcThermalSensorsCallback callback) {
        std::move(callback).Run({}, std::nullopt);
      });

  auto result = FetchThermalInfoSync();
  ASSERT_TRUE(result->is_thermal_info());

  const auto& info = result->get_thermal_info();
  EXPECT_EQ(info->thermal_sensors.size(), 2);
  auto first_sysfs_response = mojom::ThermalSensorInfo::New(
      kFirstThermalZoneType,
      TemperatureMillicelsiusToDoubleCelsius(kFirstThermalZoneTemp),
      mojom::ThermalSensorInfo::ThermalSensorSource::kSysFs);
  auto second_sysfs_response = mojom::ThermalSensorInfo::New(
      kSecondThermalZoneType,
      TemperatureMillicelsiusToDoubleCelsius(kSecondThermalZoneTemp),
      mojom::ThermalSensorInfo::ThermalSensorSource::kSysFs);
  EXPECT_THAT(
      info->thermal_sensors,
      UnorderedElementsAreArray(
          {MatchesThermalSensorInfo(std::cref(first_sysfs_response)),
           MatchesThermalSensorInfo(std::cref(second_sysfs_response))}));
}

// Test that fetcher succeeds when EC fetch fails with error.
TEST_F(ThermalFetcherTest, TestEcErrorFetchFailure) {
  EXPECT_CALL(*mock_executor(), GetEcThermalSensors(_))
      .WillOnce([](mojom::Executor::GetEcThermalSensorsCallback callback) {
        std::move(callback).Run({}, kEcFailureMessage);
      });

  auto result = FetchThermalInfoSync();
  ASSERT_TRUE(result->is_thermal_info());

  const auto& info = result->get_thermal_info();
  EXPECT_EQ(info->thermal_sensors.size(), 2);
  auto first_sysfs_response = mojom::ThermalSensorInfo::New(
      kFirstThermalZoneType,
      TemperatureMillicelsiusToDoubleCelsius(kFirstThermalZoneTemp),
      mojom::ThermalSensorInfo::ThermalSensorSource::kSysFs);
  auto second_sysfs_response = mojom::ThermalSensorInfo::New(
      kSecondThermalZoneType,
      TemperatureMillicelsiusToDoubleCelsius(kSecondThermalZoneTemp),
      mojom::ThermalSensorInfo::ThermalSensorSource::kSysFs);
  EXPECT_THAT(
      info->thermal_sensors,
      UnorderedElementsAreArray(
          {MatchesThermalSensorInfo(std::cref(first_sysfs_response)),
           MatchesThermalSensorInfo(std::cref(second_sysfs_response))}));
}

}  // namespace
}  // namespace diagnostics
