// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/system/sensor_service_handler.h"

#include <memory>
#include <utility>

#include <gtest/gtest.h>
#include <mojo/public/cpp/bindings/receiver.h>
#include <mojo/public/cpp/bindings/remote.h>

#include "power_manager/powerd/system/fake_sensor_device.h"
#include "power_manager/powerd/system/fake_sensor_service.h"

namespace power_manager {
namespace system {

namespace {

class FakeObserver : public SensorServiceHandlerObserver {
 public:
  explicit FakeObserver(SensorServiceHandler* sensor_service_handler)
      : SensorServiceHandlerObserver(sensor_service_handler) {}

  // SensorServiceHandlerObserver overrides:
  void OnNewDeviceAdded(
      int32_t iio_device_id,
      const std::vector<cros::mojom::DeviceType>& types) override {
    device_ids_.push_back(iio_device_id);
  }
  void SensorServiceConnected() override { connected_ = true; }
  void SensorServiceDisconnected() override { connected_ = false; }

  std::vector<int32_t> device_ids_;
  base::Optional<bool> connected_;
};

}  // namespace

class SensorServiceHandlerTest : public ::testing::Test {
 public:
  SensorServiceHandlerTest(const SensorServiceHandlerTest&) = delete;
  SensorServiceHandlerTest& operator=(const SensorServiceHandlerTest&) = delete;

  SensorServiceHandlerTest() {}
  ~SensorServiceHandlerTest() override {}

 protected:
  void SetUp() override {
    observer_ = std::make_unique<FakeObserver>(&sensor_service_handler_);
    ResetMojoChannel();
  }

  void ResetMojoChannel() {
    sensor_service_.ClearReceivers();

    // Wait until the disconnect handler in |sensor_service_handler_| is called.
    base::RunLoop().RunUntilIdle();

    mojo::PendingRemote<cros::mojom::SensorService> pending_remote;
    sensor_service_.AddReceiver(
        pending_remote.InitWithNewPipeAndPassReceiver());
    sensor_service_handler_.SetUpChannel(std::move(pending_remote));
  }

  void SetSensor(int32_t iio_device_id) {
    auto sensor_device =
        std::make_unique<FakeSensorDevice>(false, base::nullopt, base::nullopt);

    sensor_service_.SetSensorDevice(iio_device_id, std::move(sensor_device));
  }

  SensorServiceHandler sensor_service_handler_;
  std::unique_ptr<FakeObserver> observer_;

  FakeSensorService sensor_service_;
};

TEST_F(SensorServiceHandlerTest, BindSensorHalClient) {
  base::RunLoop loop;

  mojo::Remote<cros::mojom::SensorHalClient> remote;
  sensor_service_handler_.BindSensorHalClient(
      remote.BindNewPipeAndPassReceiver(), loop.QuitClosure());

  EXPECT_TRUE(sensor_service_.HasReceivers());

  remote.reset();
  // Wait until |sensor_service_handler.on_mojo_disconnect_callback_| is called.
  loop.Run();

  // Wait until |sensor_service_|'s disconnection callback is done.
  base::RunLoop().RunUntilIdle();

  EXPECT_FALSE(sensor_service_.HasReceivers());
}

TEST_F(SensorServiceHandlerTest, ConnectedAndAddNewDevices) {
  EXPECT_TRUE(observer_->connected_.value_or(false));

  SetSensor(1);
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(observer_->device_ids_.size(), 1);
  EXPECT_EQ(observer_->device_ids_[0], 1);

  auto observer2 = std::make_unique<FakeObserver>(&sensor_service_handler_);
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(observer2->device_ids_.size(), 1);
  EXPECT_EQ(observer2->device_ids_[0], 1);
}

}  // namespace system
}  // namespace power_manager
