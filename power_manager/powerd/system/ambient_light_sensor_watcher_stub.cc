// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/system/ambient_light_sensor_watcher_stub.h"

namespace power_manager {
namespace system {

const std::vector<AmbientLightSensorInfo>&
AmbientLightSensorWatcherStub::GetAmbientLightSensors() const {
  return ambient_light_sensors_;
}

void AmbientLightSensorWatcherStub::AddObserver(
    AmbientLightSensorWatcherObserver* observer) {
  DCHECK(observer);
  observers_.AddObserver(observer);
}

void AmbientLightSensorWatcherStub::RemoveObserver(
    AmbientLightSensorWatcherObserver* observer) {
  DCHECK(observer);
  observers_.RemoveObserver(observer);
}

void AmbientLightSensorWatcherStub::AddSensor(
    const AmbientLightSensorInfo& device_info) {
  for (const auto& sensor : ambient_light_sensors_) {
    if (sensor == device_info) {
      return;
    }
  }

  ambient_light_sensors_.push_back(device_info);

  for (auto& observer : observers_) {
    observer.OnAmbientLightSensorsChanged(ambient_light_sensors_);
  }
}

void AmbientLightSensorWatcherStub::RemoveSensor(
    const AmbientLightSensorInfo& device_info) {
  bool found = false;
  for (auto it = ambient_light_sensors_.begin();
       it != ambient_light_sensors_.end(); it++) {
    if (*it == device_info) {
      ambient_light_sensors_.erase(it);
      found = true;
      break;
    }
  }

  if (!found) {
    return;
  }

  for (auto& observer : observers_) {
    observer.OnAmbientLightSensorsChanged(ambient_light_sensors_);
  }
}

}  // namespace system
}  // namespace power_manager
