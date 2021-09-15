// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/system/ambient_light_sensor_manager_mojo.h"

#include <memory>
#include <utility>

#include <base/bind.h>
#include <base/optional.h>
#include <base/run_loop.h>
#include <gtest/gtest.h>

#include "power_manager/common/fake_prefs.h"
#include "power_manager/powerd/system/ambient_light_sensor_delegate_mojo.h"
#include "power_manager/powerd/system/fake_sensor_device.h"
#include "power_manager/powerd/system/fake_sensor_service.h"

namespace power_manager {
namespace system {

namespace {

constexpr int32_t kFakeAcpiAlsId = 0;
constexpr int32_t kFakeBaseId = 1;
constexpr int32_t kFakeLidId = 2;

}  // namespace

class AmbientLightSensorManagerMojoTest : public ::testing::Test {
 public:
  AmbientLightSensorManagerMojoTest(const AmbientLightSensorManagerMojoTest&) =
      delete;
  AmbientLightSensorManagerMojoTest& operator=(
      const AmbientLightSensorManagerMojoTest&) = delete;

  AmbientLightSensorManagerMojoTest() {}
  ~AmbientLightSensorManagerMojoTest() override {}

 protected:
  void TearDown() override { manager_.reset(); }

  void SetManager() {
    manager_ = std::make_unique<AmbientLightSensorManagerMojo>(&prefs_);
    if (!manager_->GetSensorForInternalBacklight())
      return;

    ResetMojoChannel();
  }

  void ResetMojoChannel() {
    sensor_service_.ClearReceivers();

    mojo::PendingRemote<cros::mojom::SensorService> pending_remote;
    sensor_service_.AddReceiver(
        pending_remote.InitWithNewPipeAndPassReceiver());
    manager_->SetUpChannel(std::move(pending_remote));
  }

  void SetSensor(int32_t iio_device_id,
                 bool is_color_sensor,
                 base::Optional<std::string> name,
                 base::Optional<std::string> location) {
    auto sensor_device = std::make_unique<FakeSensorDevice>(
        is_color_sensor, std::move(name), std::move(location));
    sensor_devices_[iio_device_id] = sensor_device.get();

    sensor_service_.SetSensorDevice(iio_device_id, std::move(sensor_device));
  }

  void SetLidSensor(bool is_color_sensor, base::Optional<std::string> name) {
    SetSensor(kFakeLidId, is_color_sensor, std::move(name),
              cros::mojom::kLocationLid);
  }

  void SetBaseSensor(base::Optional<std::string> name) {
    SetSensor(kFakeBaseId, /*is_color_sensor=*/false, std::move(name),
              cros::mojom::kLocationBase);
  }

  FakePrefs prefs_;

  FakeSensorService sensor_service_;
  std::map<int32_t, FakeSensorDevice*> sensor_devices_;

  std::unique_ptr<AmbientLightSensorManagerMojo> manager_;
};

TEST_F(AmbientLightSensorManagerMojoTest, ZeroSensors) {
  prefs_.SetInt64(kHasAmbientLightSensorPref, 0);
  prefs_.SetInt64(kAllowAmbientEQ, 0);

  SetManager();
  EXPECT_FALSE(manager_->GetSensorForInternalBacklight());
  EXPECT_FALSE(manager_->GetSensorForKeyboardBacklight());
}

TEST_F(AmbientLightSensorManagerMojoTest, OneColorSensor) {
  prefs_.SetInt64(kHasAmbientLightSensorPref, 1);
  prefs_.SetInt64(kAllowAmbientEQ, 1);

  SetLidSensor(/*is_color_sensor=*/true, kCrosECLightName);
  SetBaseSensor(/*name=*/base::nullopt);

  SetManager();
  EXPECT_FALSE(manager_->HasColorSensor());

  // Wait until all initialization tasks are done.
  base::RunLoop().RunUntilIdle();

  auto internal_backlight_sensor = manager_->GetSensorForInternalBacklight();
  auto keyboard_backlight_sensor = manager_->GetSensorForKeyboardBacklight();
  EXPECT_TRUE(internal_backlight_sensor);
  EXPECT_EQ(internal_backlight_sensor, keyboard_backlight_sensor);

  EXPECT_TRUE(manager_->HasColorSensor());

  EXPECT_TRUE(sensor_devices_[kFakeLidId]->HasReceivers());
  EXPECT_FALSE(sensor_devices_[kFakeBaseId]->HasReceivers());

  // Simulate a disconnection between |manager_| and IIO Service.
  ResetMojoChannel();

  // Wait until all reconnection tasks are done.
  base::RunLoop().RunUntilIdle();

  EXPECT_TRUE(manager_->HasColorSensor());

  EXPECT_TRUE(sensor_devices_[kFakeLidId]->HasReceivers());
  EXPECT_FALSE(sensor_devices_[kFakeBaseId]->HasReceivers());
}

TEST_F(AmbientLightSensorManagerMojoTest, TwoSensorsNoColor) {
  prefs_.SetInt64(kHasAmbientLightSensorPref, 2);
  prefs_.SetInt64(kAllowAmbientEQ, 0);

  SetSensor(kFakeAcpiAlsId,
            /*is_color_sensor=*/false, kAcpiAlsName,
            /*location=*/base::nullopt);
  SetLidSensor(/*is_color_sensor=*/false, kCrosECLightName);
  SetBaseSensor(kCrosECLightName);

  SetManager();
  EXPECT_FALSE(manager_->HasColorSensor());

  // Wait until all initialization tasks are done.
  base::RunLoop().RunUntilIdle();

  auto internal_backlight_sensor = manager_->GetSensorForInternalBacklight();
  auto keyboard_backlight_sensor = manager_->GetSensorForKeyboardBacklight();

  EXPECT_NE(internal_backlight_sensor, keyboard_backlight_sensor);
  EXPECT_FALSE(manager_->HasColorSensor());
  EXPECT_FALSE(internal_backlight_sensor->IsColorSensor());
  EXPECT_FALSE(keyboard_backlight_sensor->IsColorSensor());

  EXPECT_TRUE(sensor_devices_[kFakeLidId]->HasReceivers());
  EXPECT_TRUE(sensor_devices_[kFakeBaseId]->HasReceivers());

  ResetMojoChannel();

  // Wait until all reconnection tasks are done.
  base::RunLoop().RunUntilIdle();

  EXPECT_FALSE(manager_->HasColorSensor());

  EXPECT_TRUE(sensor_devices_[kFakeLidId]->HasReceivers());
  EXPECT_TRUE(sensor_devices_[kFakeBaseId]->HasReceivers());
}

TEST_F(AmbientLightSensorManagerMojoTest, AeqWithNoColorSensor) {
  prefs_.SetInt64(kHasAmbientLightSensorPref, 2);
  prefs_.SetInt64(kAllowAmbientEQ, 1);

  SetLidSensor(/*is_color_sensor=*/false, kCrosECLightName);
  SetBaseSensor(kCrosECLightName);

  SetManager();

  // Wait until all initialization tasks are done.
  base::RunLoop().RunUntilIdle();

  auto internal_backlight_sensor = manager_->GetSensorForInternalBacklight();
  auto keyboard_backlight_sensor = manager_->GetSensorForKeyboardBacklight();

  EXPECT_NE(internal_backlight_sensor, keyboard_backlight_sensor);
  EXPECT_FALSE(manager_->HasColorSensor());

  EXPECT_TRUE(sensor_devices_[kFakeLidId]->HasReceivers());
  EXPECT_TRUE(sensor_devices_[kFakeBaseId]->HasReceivers());

  ResetMojoChannel();

  // Wait until all reconnection tasks are done.
  base::RunLoop().RunUntilIdle();

  EXPECT_FALSE(manager_->HasColorSensor());

  EXPECT_TRUE(sensor_devices_[kFakeLidId]->HasReceivers());
  EXPECT_TRUE(sensor_devices_[kFakeBaseId]->HasReceivers());
}

TEST_F(AmbientLightSensorManagerMojoTest, AeqWithColorSensor) {
  prefs_.SetInt64(kHasAmbientLightSensorPref, 2);
  prefs_.SetInt64(kAllowAmbientEQ, 1);

  SetLidSensor(/*is_color_sensor=*/true, kCrosECLightName);
  SetBaseSensor(kCrosECLightName);

  SetManager();

  // Wait until all initialization tasks are done.
  base::RunLoop().RunUntilIdle();

  auto internal_backlight_sensor = manager_->GetSensorForInternalBacklight();
  auto keyboard_backlight_sensor = manager_->GetSensorForKeyboardBacklight();

  EXPECT_NE(internal_backlight_sensor, keyboard_backlight_sensor);
  EXPECT_TRUE(manager_->HasColorSensor());
  EXPECT_TRUE(internal_backlight_sensor->IsColorSensor());
  EXPECT_FALSE(keyboard_backlight_sensor->IsColorSensor());

  EXPECT_TRUE(sensor_devices_[kFakeLidId]->HasReceivers());
  EXPECT_TRUE(sensor_devices_[kFakeBaseId]->HasReceivers());

  ResetMojoChannel();

  // Wait until all reconnection tasks are done.
  base::RunLoop().RunUntilIdle();

  EXPECT_TRUE(manager_->HasColorSensor());

  EXPECT_TRUE(sensor_devices_[kFakeLidId]->HasReceivers());
  EXPECT_TRUE(sensor_devices_[kFakeBaseId]->HasReceivers());
}

TEST_F(AmbientLightSensorManagerMojoTest, OneLateColorSensor) {
  prefs_.SetInt64(kHasAmbientLightSensorPref, 1);
  prefs_.SetInt64(kAllowAmbientEQ, 1);

  SetBaseSensor(/*name=*/base::nullopt);

  SetManager();
  EXPECT_FALSE(manager_->HasColorSensor());

  // Wait until all initialization tasks are done.
  base::RunLoop().RunUntilIdle();

  auto internal_backlight_sensor = manager_->GetSensorForInternalBacklight();
  auto keyboard_backlight_sensor = manager_->GetSensorForKeyboardBacklight();
  EXPECT_TRUE(internal_backlight_sensor);
  EXPECT_EQ(internal_backlight_sensor, keyboard_backlight_sensor);

  EXPECT_FALSE(manager_->HasColorSensor());

  EXPECT_TRUE(sensor_devices_[kFakeBaseId]->HasReceivers());

  SetLidSensor(/*is_color_sensor=*/true, kCrosECLightName);

  // Wait until all initialization tasks are done.
  base::RunLoop().RunUntilIdle();

  EXPECT_TRUE(manager_->HasColorSensor());

  EXPECT_TRUE(sensor_devices_[kFakeLidId]->HasReceivers());
  EXPECT_FALSE(sensor_devices_[kFakeBaseId]->HasReceivers());
}

TEST_F(AmbientLightSensorManagerMojoTest, AeqWithLateColorSensor) {
  prefs_.SetInt64(kHasAmbientLightSensorPref, 2);
  prefs_.SetInt64(kAllowAmbientEQ, 1);

  SetBaseSensor(kCrosECLightName);

  SetManager();

  // Wait until all initialization tasks are done.
  base::RunLoop().RunUntilIdle();

  auto internal_backlight_sensor = manager_->GetSensorForInternalBacklight();
  auto keyboard_backlight_sensor = manager_->GetSensorForKeyboardBacklight();

  EXPECT_NE(internal_backlight_sensor, keyboard_backlight_sensor);
  EXPECT_FALSE(manager_->HasColorSensor());
  EXPECT_FALSE(keyboard_backlight_sensor->IsColorSensor());

  EXPECT_TRUE(sensor_devices_[kFakeBaseId]->HasReceivers());

  SetLidSensor(/*is_color_sensor=*/true, kCrosECLightName);

  // Wait until all initialization tasks are done.
  base::RunLoop().RunUntilIdle();

  EXPECT_TRUE(manager_->HasColorSensor());
  EXPECT_TRUE(internal_backlight_sensor->IsColorSensor());
  EXPECT_FALSE(keyboard_backlight_sensor->IsColorSensor());

  EXPECT_TRUE(sensor_devices_[kFakeLidId]->HasReceivers());
}

TEST_F(AmbientLightSensorManagerMojoTest, DeviceRemovedWithOneColorSensor) {
  prefs_.SetInt64(kHasAmbientLightSensorPref, 1);
  prefs_.SetInt64(kAllowAmbientEQ, 1);

  SetSensor(kFakeAcpiAlsId,
            /*is_color_sensor=*/false, kAcpiAlsName,
            /*location=*/base::nullopt);
  SetLidSensor(/*is_color_sensor=*/true, kCrosECLightName);
  SetBaseSensor(/*name=*/base::nullopt);

  SetManager();
  EXPECT_FALSE(manager_->HasColorSensor());

  // Wait until all initialization tasks are done.
  base::RunLoop().RunUntilIdle();

  auto internal_backlight_sensor = manager_->GetSensorForInternalBacklight();
  auto keyboard_backlight_sensor = manager_->GetSensorForKeyboardBacklight();
  EXPECT_TRUE(internal_backlight_sensor);
  EXPECT_EQ(internal_backlight_sensor, keyboard_backlight_sensor);

  EXPECT_TRUE(manager_->HasColorSensor());

  EXPECT_TRUE(sensor_devices_[kFakeLidId]->HasReceivers());
  EXPECT_FALSE(sensor_devices_[kFakeBaseId]->HasReceivers());

  sensor_devices_[kFakeAcpiAlsId]->ClearReceiverWithReason(
      cros::mojom::SensorDeviceDisconnectReason::DEVICE_REMOVED,
      "Device was removed");

  // Wait until all reconnection tasks are done.
  base::RunLoop().RunUntilIdle();

  // The sensor service and other mojo pipes are not reset with the reason:
  // DEVICE_REMOVED.
  EXPECT_TRUE(sensor_devices_[kFakeLidId]->HasReceivers());

  sensor_devices_[kFakeLidId]->ClearReceiverWithReason(
      cros::mojom::SensorDeviceDisconnectReason::DEVICE_REMOVED,
      "Device was removed");
  // Overwrite the lid and base light sensors in the iioservice.
  SetLidSensor(/*is_color_sensor=*/true, /*name=*/base::nullopt);
  SetBaseSensor(kCrosECLightName);

  // Wait until all reconnection tasks are done.
  base::RunLoop().RunUntilIdle();

  // Choose the base light sensor as it has the name attribute: cros-ec-light.
  internal_backlight_sensor = manager_->GetSensorForInternalBacklight();
  keyboard_backlight_sensor = manager_->GetSensorForKeyboardBacklight();
  EXPECT_TRUE(internal_backlight_sensor);
  EXPECT_EQ(internal_backlight_sensor, keyboard_backlight_sensor);

  EXPECT_FALSE(manager_->HasColorSensor());

  EXPECT_FALSE(sensor_devices_[kFakeLidId]->HasReceivers());
  EXPECT_TRUE(sensor_devices_[kFakeBaseId]->HasReceivers());
}

TEST_F(AmbientLightSensorManagerMojoTest, DeviceRemovedWithTwoSensors) {
  prefs_.SetInt64(kHasAmbientLightSensorPref, 2);
  prefs_.SetInt64(kAllowAmbientEQ, 1);

  SetSensor(kFakeAcpiAlsId,
            /*is_color_sensor=*/false, kAcpiAlsName,
            /*location=*/base::nullopt);
  SetLidSensor(/*is_color_sensor=*/true, kCrosECLightName);
  SetBaseSensor(/*name=*/kCrosECLightName);

  SetManager();
  EXPECT_FALSE(manager_->HasColorSensor());

  // Wait until all initialization tasks are done.
  base::RunLoop().RunUntilIdle();

  auto internal_backlight_sensor = manager_->GetSensorForInternalBacklight();
  auto keyboard_backlight_sensor = manager_->GetSensorForKeyboardBacklight();
  EXPECT_TRUE(internal_backlight_sensor);
  EXPECT_NE(internal_backlight_sensor, keyboard_backlight_sensor);

  EXPECT_TRUE(manager_->HasColorSensor());

  EXPECT_TRUE(sensor_devices_[kFakeLidId]->HasReceivers());
  EXPECT_TRUE(sensor_devices_[kFakeBaseId]->HasReceivers());

  sensor_devices_[kFakeAcpiAlsId]->ClearReceiverWithReason(
      cros::mojom::SensorDeviceDisconnectReason::DEVICE_REMOVED,
      "Device was removed");

  // Wait until all reconnection tasks are done.
  base::RunLoop().RunUntilIdle();

  // The sensor service and other mojo pipes are not reset with the reason:
  // DEVICE_REMOVED.
  EXPECT_TRUE(sensor_devices_[kFakeLidId]->HasReceivers());
  EXPECT_TRUE(sensor_devices_[kFakeBaseId]->HasReceivers());

  sensor_devices_[kFakeLidId]->ClearReceiverWithReason(
      cros::mojom::SensorDeviceDisconnectReason::DEVICE_REMOVED,
      "Device was removed");
  // Overwrite the lid and base light sensors in the iioservice.
  SetLidSensor(/*is_color_sensor=*/true, /*name=*/base::nullopt);

  // Wait until all reconnection tasks are done.
  base::RunLoop().RunUntilIdle();

  // Choose the base light sensor as it has the name attribute: cros-ec-light.
  internal_backlight_sensor = manager_->GetSensorForInternalBacklight();
  keyboard_backlight_sensor = manager_->GetSensorForKeyboardBacklight();
  EXPECT_TRUE(internal_backlight_sensor);
  EXPECT_NE(internal_backlight_sensor, keyboard_backlight_sensor);

  EXPECT_FALSE(manager_->HasColorSensor());

  EXPECT_FALSE(sensor_devices_[kFakeLidId]->HasReceivers());
  EXPECT_TRUE(sensor_devices_[kFakeBaseId]->HasReceivers());
}

}  // namespace system
}  // namespace power_manager
