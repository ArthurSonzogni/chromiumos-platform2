// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/system/ambient_light_sensor_watcher_mojo.h"

#include <map>
#include <memory>
#include <string>
#include <utility>

#include <base/optional.h>
#include <base/run_loop.h>
#include <gtest/gtest.h>

#include "power_manager/powerd/system/ambient_light_sensor_watcher_observer_stub.h"
#include "power_manager/powerd/system/fake_sensor_device.h"
#include "power_manager/powerd/system/fake_sensor_service.h"
#include "power_manager/powerd/system/sensor_service_handler.h"

namespace power_manager {
namespace system {

namespace {

constexpr char kBadSyspath[] =
    "/sys/my/mock/device/HID-SENSOR-200040/more/mock/path";

constexpr char kGoodSyspath[] =
    "/sys/my/mock/device/HID-SENSOR-200041/more/mock/path";

constexpr char kGoodSyspath2[] =
    "/sys/my/mock/device/HID-SENSOR-200041/more/mock/path";

}  // namespace

class AmbientLightSensorWatcherMojoTest : public testing::Test {
 public:
  AmbientLightSensorWatcherMojoTest() {}
  ~AmbientLightSensorWatcherMojoTest() override {}

 protected:
  void SetUp() override {
    watcher_ = std::make_unique<AmbientLightSensorWatcherMojo>(
        &sensor_service_handler_);

    auto sensor_device = std::make_unique<FakeSensorDevice>(
        false /*is_color_sensor*/, base::nullopt /*name*/,
        cros::mojom::kLocationLid);

    SetSensor(kBadSyspath);  // id = 0

    mojo::PendingRemote<cros::mojom::SensorService> pending_remote;
    sensor_service_.AddReceiver(
        pending_remote.InitWithNewPipeAndPassReceiver());
    sensor_service_handler_.SetUpChannel(std::move(pending_remote));
  }

  void SetSensor(std::string sys_path) {
    auto sensor_device = std::make_unique<FakeSensorDevice>(
        false /*is_color_sensor*/, base::nullopt /*name*/,
        cros::mojom::kLocationLid);
    sensor_device->SetAttribute(cros::mojom::kSysPath, sys_path);

    auto iio_device_id = sensor_num_++;
    sensor_devices_[iio_device_id] = sensor_device.get();
    sensor_service_.SetSensorDevice(iio_device_id, std::move(sensor_device));
  }

  FakeSensorService sensor_service_;
  std::map<int32_t, FakeSensorDevice*> sensor_devices_;

  SensorServiceHandler sensor_service_handler_;

  std::unique_ptr<AmbientLightSensorWatcherMojo> watcher_;

  int32_t sensor_num_ = 0;
};

TEST_F(AmbientLightSensorWatcherMojoTest, Basic) {
  AmbientLightSensorWatcherObserverStub observer(watcher_.get());

  // Wait until all initialization tasks are done.
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(0, observer.num_als_changes());

  SetSensor(kGoodSyspath);  // id = 1

  // Wait until all tasks are done.
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(1, observer.num_als_changes());
  EXPECT_EQ(1, observer.num_als());

  sensor_service_.ClearReceivers();

  // Wait until all tasks are done.
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(2, observer.num_als_changes());
  EXPECT_EQ(0, observer.num_als());
}

TEST_F(AmbientLightSensorWatcherMojoTest, SensorDeviceDisconnect) {
  AmbientLightSensorWatcherObserverStub observer(watcher_.get());

  SetSensor(kGoodSyspath);   // id = 1
  SetSensor(kGoodSyspath2);  // id = 2

  // Wait until all tasks are done.
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(2, observer.num_als_changes());
  EXPECT_EQ(2, observer.num_als());

  EXPECT_TRUE(sensor_devices_[1]->HasReceivers());
  EXPECT_TRUE(sensor_devices_[2]->HasReceivers());

  sensor_devices_[2]->ClearReceiverWithReason(
      cros::mojom::SensorDeviceDisconnectReason::DEVICE_REMOVED,
      "Device was removed");

  // Wait until all tasks are done.
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(3, observer.num_als_changes());
  EXPECT_EQ(1, observer.num_als());

  EXPECT_TRUE(sensor_devices_[1]->HasReceivers());
  EXPECT_FALSE(sensor_devices_[2]->HasReceivers());

  sensor_devices_[0]->ClearReceiverWithReason(
      cros::mojom::SensorDeviceDisconnectReason::IIOSERVICE_CRASHED,
      "iioservice crashed");

  // Wait until all tasks are done.
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(4, observer.num_als_changes());
  EXPECT_EQ(0, observer.num_als());

  EXPECT_FALSE(sensor_devices_[1]->HasReceivers());
  EXPECT_FALSE(sensor_devices_[2]->HasReceivers());
}

}  // namespace system
}  // namespace power_manager
