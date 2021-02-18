// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/system/ambient_light_sensor_watcher.h"

#include <gtest/gtest.h>
#include <string>

#include "power_manager/powerd/system/udev_stub.h"

namespace power_manager {
namespace system {

namespace {

constexpr char kGoodSysname[] = "iio:device0";
constexpr char kGoodSyspath[] =
    "/sys/my/mock/device/HID-SENSOR-200041/more/mock/path";

// Stub implementation of AmbientLightSensorWatcherObserver.
class TestObserver : public AmbientLightSensorWatcherObserver {
 public:
  explicit TestObserver(AmbientLightSensorWatcher* watcher)
      : watcher_(watcher), num_als_changes_(0) {
    watcher_->AddObserver(this);
  }
  TestObserver(const TestObserver&) = delete;
  TestObserver& operator=(const TestObserver&) = delete;

  virtual ~TestObserver() { watcher_->RemoveObserver(this); }

  int num_als_changes() const { return num_als_changes_; }

  // AmbientLightSensorWatcherObserver implementation:
  void OnAmbientLightSensorsChanged(
      const std::vector<AmbientLightSensorInfo>& displays) override {
    num_als_changes_++;
  }

 private:
  AmbientLightSensorWatcher* watcher_;  // Not owned.
  // Number of times that OnAmbientLightSensorsChanged() has been called.
  int num_als_changes_;
};

}  // namespace

class AmbientLightSensorWatcherTest : public testing::Test {
 public:
  AmbientLightSensorWatcherTest() {}
  ~AmbientLightSensorWatcherTest() override {}

 protected:
  void Init() { watcher_.Init(&udev_); }

  // Add a sensor device to the udev stub so that it will show up as already
  // connected when the AmbientLightSensorWatcher is initialized.
  void AddExistingDevice() {
    UdevDeviceInfo device_info;
    device_info.subsystem = AmbientLightSensorWatcher::kIioUdevSubsystem;
    device_info.devtype = AmbientLightSensorWatcher::kIioUdevDevice;
    device_info.sysname = kGoodSysname;
    device_info.syspath = kGoodSyspath;
    udev_.AddSubsystemDevice(device_info.subsystem, device_info, {});
  }

  // Send a udev ADD event for a device with the given parameters.
  void AddDevice(const std::string& subsystem,
                 const std::string& devtype,
                 const std::string& sysname,
                 const std::string& syspath) {
    UdevEvent iio_event;
    iio_event.action = UdevEvent::Action::ADD;
    iio_event.device_info.subsystem = subsystem;
    iio_event.device_info.devtype = devtype;
    iio_event.device_info.sysname = sysname;
    iio_event.device_info.syspath = syspath;
    udev_.NotifySubsystemObservers(iio_event);
  }

  // Send a udev ADD event for the known good ALS device.
  void AddDevice() {
    AddDevice(AmbientLightSensorWatcher::kIioUdevSubsystem,
              AmbientLightSensorWatcher::kIioUdevDevice, kGoodSysname,
              kGoodSyspath);
  }

  // Send a udev REMOVE event for the known good ALS device.
  void RemoveDevice() {
    UdevEvent iio_event;
    iio_event.action = UdevEvent::Action::REMOVE;
    iio_event.device_info.subsystem =
        AmbientLightSensorWatcher::kIioUdevSubsystem;
    iio_event.device_info.devtype = AmbientLightSensorWatcher::kIioUdevDevice;
    iio_event.device_info.sysname = kGoodSysname;
    iio_event.device_info.syspath = kGoodSyspath;
    udev_.NotifySubsystemObservers(iio_event);
  }

  UdevStub udev_;
  AmbientLightSensorWatcher watcher_;
};

TEST_F(AmbientLightSensorWatcherTest, DetectExistingDevice) {
  TestObserver observer(&watcher_);
  AddExistingDevice();
  Init();
  EXPECT_EQ(1, observer.num_als_changes());
  EXPECT_EQ(1, watcher_.GetAmbientLightSensors().size());
}

TEST_F(AmbientLightSensorWatcherTest, GoodDevice) {
  TestObserver observer(&watcher_);
  Init();
  AddDevice();
  const std::vector<AmbientLightSensorInfo> sensors =
      watcher_.GetAmbientLightSensors();
  EXPECT_EQ(1, observer.num_als_changes());
  ASSERT_EQ(1, sensors.size());
  EXPECT_EQ(kGoodSyspath, sensors[0].iio_path.value());
  EXPECT_EQ(kGoodSysname, sensors[0].device);
}

TEST_F(AmbientLightSensorWatcherTest, BadDeviceWrongSubsystem) {
  TestObserver observer(&watcher_);
  Init();
  AddDevice("usb", AmbientLightSensorWatcher::kIioUdevDevice, kGoodSysname,
            kGoodSyspath);
  EXPECT_EQ(0, observer.num_als_changes());
  EXPECT_EQ(0, watcher_.GetAmbientLightSensors().size());
}

TEST_F(AmbientLightSensorWatcherTest, BadDeviceWrongDeviceType) {
  TestObserver observer(&watcher_);
  Init();
  AddDevice(AmbientLightSensorWatcher::kIioUdevSubsystem, "trigger",
            kGoodSysname, kGoodSyspath);
  EXPECT_EQ(0, observer.num_als_changes());
  EXPECT_EQ(0, watcher_.GetAmbientLightSensors().size());
}

TEST_F(AmbientLightSensorWatcherTest, BadDeviceWrongSyspath) {
  TestObserver observer(&watcher_);
  Init();
  AddDevice(AmbientLightSensorWatcher::kIioUdevSubsystem,
            AmbientLightSensorWatcher::kIioUdevDevice, kGoodSysname,
            "/sys/not/a/usb/hid/sensor");
  EXPECT_EQ(0, observer.num_als_changes());
  EXPECT_EQ(0, watcher_.GetAmbientLightSensors().size());
}

TEST_F(AmbientLightSensorWatcherTest, DuplicateDevice) {
  TestObserver observer(&watcher_);
  Init();
  AddDevice();
  AddDevice();
  EXPECT_EQ(1, observer.num_als_changes());
  EXPECT_EQ(1, watcher_.GetAmbientLightSensors().size());
}

TEST_F(AmbientLightSensorWatcherTest, RemoveDevice) {
  TestObserver observer(&watcher_);
  Init();
  AddDevice();
  EXPECT_EQ(1, observer.num_als_changes());
  EXPECT_EQ(1, watcher_.GetAmbientLightSensors().size());
  RemoveDevice();
  EXPECT_EQ(2, observer.num_als_changes());
  EXPECT_EQ(0, watcher_.GetAmbientLightSensors().size());
}

}  // namespace system
}  // namespace power_manager
