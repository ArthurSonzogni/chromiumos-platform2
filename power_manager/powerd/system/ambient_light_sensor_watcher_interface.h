// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_AMBIENT_LIGHT_SENSOR_WATCHER_INTERFACE_H_
#define POWER_MANAGER_POWERD_SYSTEM_AMBIENT_LIGHT_SENSOR_WATCHER_INTERFACE_H_

#include <vector>

#include "power_manager/powerd/system/ambient_light_sensor_info.h"
#include "power_manager/powerd/system/ambient_light_sensor_watcher_observer.h"

namespace power_manager {
namespace system {

// Watches for ambient light sensors being connected or disconnected.
class AmbientLightSensorWatcherInterface {
 public:
  virtual ~AmbientLightSensorWatcherInterface() {}

  // Returns the current list of connected ambient light sensors.
  virtual const std::vector<AmbientLightSensorInfo>& GetAmbientLightSensors()
      const = 0;

  // Adds or removes an observer.
  virtual void AddObserver(AmbientLightSensorWatcherObserver* observer) = 0;
  virtual void RemoveObserver(AmbientLightSensorWatcherObserver* observer) = 0;
};

}  // namespace system
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_SYSTEM_AMBIENT_LIGHT_SENSOR_WATCHER_INTERFACE_H_
