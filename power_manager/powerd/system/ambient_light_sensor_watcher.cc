// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/system/ambient_light_sensor_watcher.h"

#include <string>
#include <vector>

#include "power_manager/powerd/system/udev.h"

#include <base/logging.h>

namespace power_manager {
namespace system {

const char AmbientLightSensorWatcher::kIioUdevSubsystem[] = "iio";

const char AmbientLightSensorWatcher::kIioUdevDevice[] = "iio_device";

AmbientLightSensorWatcher::AmbientLightSensorWatcher() : udev_(nullptr) {}

AmbientLightSensorWatcher::~AmbientLightSensorWatcher() {
  if (udev_) {
    udev_->RemoveSubsystemObserver(kIioUdevSubsystem, this);
    udev_ = nullptr;
  }
}

void AmbientLightSensorWatcher::Init(UdevInterface* udev) {
  udev_ = udev;
  udev_->AddSubsystemObserver(kIioUdevSubsystem, this);

  std::vector<UdevDeviceInfo> iio_devices;
  if (!udev_->GetSubsystemDevices(kIioUdevSubsystem, &iio_devices)) {
    LOG(ERROR) << "Enumeration of existing iio devices failed";
  }

  for (const auto& iio_dev : iio_devices) {
    OnAddUdevDevice(iio_dev);
  }
}

const std::vector<AmbientLightSensorInfo>&
AmbientLightSensorWatcher::GetAmbientLightSensors() const {
  return ambient_light_sensors_;
}

void AmbientLightSensorWatcher::AddObserver(
    AmbientLightSensorWatcherObserver* observer) {
  observers_.AddObserver(observer);
}

void AmbientLightSensorWatcher::RemoveObserver(
    AmbientLightSensorWatcherObserver* observer) {
  observers_.RemoveObserver(observer);
}

void AmbientLightSensorWatcher::OnUdevEvent(const UdevEvent& event) {
  switch (event.action) {
    case UdevEvent::Action::ADD:
      OnAddUdevDevice(event.device_info);
      break;
    case UdevEvent::Action::REMOVE:
      OnRemoveUdevDevice(event.device_info);
      break;
    default:
      break;
  }
}

void AmbientLightSensorWatcher::NotifyObservers() {
  for (auto& observer : observers_) {
    observer.OnAmbientLightSensorsChanged(ambient_light_sensors_);
  }
}

bool AmbientLightSensorWatcher::IsAmbientLightSensor(
    const UdevDeviceInfo& device_info) {
  if (device_info.subsystem != kIioUdevSubsystem) {
    return false;
  }

  if (device_info.devtype != kIioUdevDevice) {
    return false;
  }

  if (device_info.syspath.find("HID-SENSOR-200041") != std::string::npos) {
    return true;
  }

  return false;
}

void AmbientLightSensorWatcher::OnAddUdevDevice(
    const UdevDeviceInfo& device_info) {
  if (!IsAmbientLightSensor(device_info)) {
    return;
  }

  AmbientLightSensorInfo new_als = {
      .iio_path = base::FilePath(device_info.syspath),
      .device = device_info.sysname,
  };

  for (const auto& als : ambient_light_sensors_) {
    if (als == new_als) {
      LOG(WARNING) << "Got udev ADD event for an ambient light sensor that's "
                      "already connected: "
                   << new_als.device;
      return;
    }
  }

  ambient_light_sensors_.push_back(new_als);
  NotifyObservers();
}

void AmbientLightSensorWatcher::OnRemoveUdevDevice(
    const UdevDeviceInfo& device_info) {
  if (!IsAmbientLightSensor(device_info)) {
    return;
  }

  AmbientLightSensorInfo to_be_removed = {
      .iio_path = base::FilePath(device_info.syspath),
      .device = device_info.sysname,
  };

  for (auto itr = ambient_light_sensors_.begin();
       itr != ambient_light_sensors_.end(); itr++) {
    if (*itr == to_be_removed) {
      ambient_light_sensors_.erase(itr);
      break;
    }
  }
  NotifyObservers();
}

}  // namespace system
}  // namespace power_manager
