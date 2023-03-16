// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/test/test_future.h>
#include <base/test/task_environment.h>
#include <gtest/gtest.h>

#include "diagnostics/cros_healthd/routines/sensor/sensor_config_checker.h"
#include "diagnostics/cros_healthd/system/fake_mojo_service.h"
#include "diagnostics/cros_healthd/system/mock_context.h"

namespace diagnostics {
namespace {

class SensorConfigCheckerTest : public testing::Test {
 protected:
  SensorConfigCheckerTest() = default;
  SensorConfigCheckerTest(const SensorConfigCheckerTest&) = delete;
  SensorConfigCheckerTest& operator=(const SensorConfigCheckerTest&) = delete;

  void SetUp() override {
    mock_context_.fake_mojo_service()->InitializeFakeMojoService();
  }

  FakeSensorService& fake_sensor_service() {
    return mock_context_.fake_mojo_service()->fake_sensor_service();
  }

  FakeSystemConfig* fake_system_config() {
    return mock_context_.fake_system_config();
  }

  std::string GetSensorLocation(SensorConfig sensor) {
    switch (sensor) {
      case kBaseAccelerometer:
      case kBaseGyroscope:
      case kBaseMagnetometer:
        return cros::mojom::kLocationBase;
      case kLidAccelerometer:
      case kLidGyroscope:
      case kLidMagnetometer:
        return cros::mojom::kLocationLid;
    }
  }

  cros::mojom::DeviceType GetSensorType(SensorConfig sensor) {
    switch (sensor) {
      case kBaseAccelerometer:
      case kLidAccelerometer:
        return cros::mojom::DeviceType::ACCEL;
      case kBaseGyroscope:
      case kLidGyroscope:
        return cros::mojom::DeviceType::ANGLVEL;
      case kBaseMagnetometer:
      case kLidMagnetometer:
        return cros::mojom::DeviceType::MAGN;
    }
  }

  base::flat_map<int32_t, std::vector<cros::mojom::DeviceType>>
  SetupSensorDevice(std::vector<SensorConfig> present_sensors) {
    base::flat_map<int32_t, std::vector<cros::mojom::DeviceType>> ids_types{};

    for (const auto& sensor : present_sensors) {
      // Get unique sensor id from enum.
      const auto& device_id = static_cast<int32_t>(sensor);
      auto device = std::make_unique<FakeSensorDevice>(
          /*name=*/std::nullopt, GetSensorLocation(sensor));
      fake_sensor_service().SetSensorDevice(device_id, std::move(device));

      // Prepare fake data for sensor checker.
      ids_types.insert({device_id, {GetSensorType(sensor)}});
    }

    // Setup fake sensors.
    fake_sensor_service().SetIdsTypes(ids_types);
    return ids_types;
  }

  bool VerifySensorInfoSync(std::vector<SensorConfig> present_sensors) {
    base::test::TestFuture<bool> future;
    const auto& ids_types = SetupSensorDevice(present_sensors);
    sensor_checker_.VerifySensorInfo(ids_types, future.GetCallback());
    return future.Get();
  }

 private:
  base::test::TaskEnvironment task_environment_;
  MockContext mock_context_;
  SensorConfigChecker sensor_checker_{mock_context_.mojo_service(),
                                      mock_context_.fake_system_config()};
};

TEST_F(SensorConfigCheckerTest, PassWithAllSensorsPresent) {
  const auto& present_sensors = {kBaseAccelerometer, kLidAccelerometer,
                                 kBaseGyroscope,     kLidGyroscope,
                                 kBaseMagnetometer,  kLidMagnetometer};
  // Setup fake configurations.
  for (const auto& sensor : present_sensors) {
    fake_system_config()->SetSensor(sensor, true);
  }

  EXPECT_TRUE(VerifySensorInfoSync(present_sensors));
}

TEST_F(SensorConfigCheckerTest, PassWithNoSensor) {
  // Setup fake configurations.
  for (const auto& sensor :
       {kBaseAccelerometer, kLidAccelerometer, kBaseGyroscope, kLidGyroscope,
        kBaseMagnetometer, kLidMagnetometer}) {
    fake_system_config()->SetSensor(sensor, false);
  }

  EXPECT_TRUE(VerifySensorInfoSync(/*present_sensors=*/{}));
}

TEST_F(SensorConfigCheckerTest, PassWithNullConfig) {
  const auto& present_sensors = {kBaseAccelerometer, kBaseGyroscope,
                                 kLidGyroscope, kLidMagnetometer};
  // Setup fake configurations.
  for (const auto& sensor :
       {kBaseAccelerometer, kLidAccelerometer, kBaseGyroscope, kLidGyroscope,
        kBaseMagnetometer, kLidMagnetometer}) {
    fake_system_config()->SetSensor(sensor, std::nullopt);
  }

  EXPECT_TRUE(VerifySensorInfoSync(present_sensors));
}

TEST_F(SensorConfigCheckerTest, FailWithUnexpectedBaseAccelerometer) {
  const auto& present_sensors = {kBaseAccelerometer};
  // Setup fake configurations.
  fake_system_config()->SetSensor(kBaseAccelerometer, false);

  EXPECT_FALSE(VerifySensorInfoSync(present_sensors));
}

TEST_F(SensorConfigCheckerTest, FailWithUnexpectedBaseGyroscope) {
  const auto& present_sensors = {kBaseGyroscope};
  // Setup fake configurations.
  fake_system_config()->SetSensor(kBaseGyroscope, false);

  EXPECT_FALSE(VerifySensorInfoSync(present_sensors));
}

TEST_F(SensorConfigCheckerTest, FailWithUnexpectedBaseMagnetometer) {
  const auto& present_sensors = {kBaseMagnetometer};
  // Setup fake configurations.
  fake_system_config()->SetSensor(kBaseMagnetometer, false);

  EXPECT_FALSE(VerifySensorInfoSync(present_sensors));
}

TEST_F(SensorConfigCheckerTest, FailWithMissingLidAccelerometer) {
  // Setup fake configurations.
  fake_system_config()->SetSensor(kLidAccelerometer, true);

  EXPECT_FALSE(VerifySensorInfoSync(/*present_sensors=*/{}));
}

TEST_F(SensorConfigCheckerTest, FailWithMissingLidGyroscope) {
  // Setup fake configurations.
  fake_system_config()->SetSensor(kLidGyroscope, true);

  EXPECT_FALSE(VerifySensorInfoSync(/*present_sensors=*/{}));
}

TEST_F(SensorConfigCheckerTest, FailWithMissingLidMagnetometer) {
  // Setup fake configurations.
  fake_system_config()->SetSensor(kLidMagnetometer, true);

  EXPECT_FALSE(VerifySensorInfoSync(/*present_sensors=*/{}));
}

}  // namespace
}  // namespace diagnostics
