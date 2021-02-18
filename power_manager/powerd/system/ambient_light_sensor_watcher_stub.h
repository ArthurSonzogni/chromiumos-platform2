// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_AMBIENT_LIGHT_SENSOR_WATCHER_STUB_H_
#define POWER_MANAGER_POWERD_SYSTEM_AMBIENT_LIGHT_SENSOR_WATCHER_STUB_H_

#include <base/observer_list.h>
#include <vector>

#include "power_manager/powerd/system/ambient_light_sensor_watcher_interface.h"

namespace power_manager {
namespace system {

// Stub implementation of AmbientLightSensorWatcherInterface for testing.
class AmbientLightSensorWatcherStub
    : public AmbientLightSensorWatcherInterface {
 public:
  AmbientLightSensorWatcherStub() = default;
  AmbientLightSensorWatcherStub(const AmbientLightSensorWatcherStub&) = delete;
  AmbientLightSensorWatcherStub& operator=(
      const AmbientLightSensorWatcherStub&) = delete;

  ~AmbientLightSensorWatcherStub() override = default;

  // AmbientLightSensorWatcherInterface implementation:
  const std::vector<AmbientLightSensorInfo>& GetAmbientLightSensors()
      const override;
  void AddObserver(AmbientLightSensorWatcherObserver* observer) override;
  void RemoveObserver(AmbientLightSensorWatcherObserver* observer) override;

  void AddSensor(const AmbientLightSensorInfo& device_info);
  void RemoveSensor(const AmbientLightSensorInfo& device_info);

 private:
  base::ObserverList<AmbientLightSensorWatcherObserver> observers_;
  // Currently-connected ambient light sensors.
  std::vector<AmbientLightSensorInfo> ambient_light_sensors_;
};

}  // namespace system
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_SYSTEM_AMBIENT_LIGHT_SENSOR_WATCHER_STUB_H_
