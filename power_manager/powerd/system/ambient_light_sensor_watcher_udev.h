// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_AMBIENT_LIGHT_SENSOR_WATCHER_UDEV_H_
#define POWER_MANAGER_POWERD_SYSTEM_AMBIENT_LIGHT_SENSOR_WATCHER_UDEV_H_

#include <iioservice/mojo/sensor.mojom.h>

#include "power_manager/powerd/system/ambient_light_sensor_info.h"
#include "power_manager/powerd/system/ambient_light_sensor_watcher_interface.h"
#include "power_manager/powerd/system/ambient_light_sensor_watcher_observer.h"
#include "power_manager/powerd/system/udev_subsystem_observer.h"

namespace power_manager {
namespace system {

// Real implementation of AmbientLightSensorWatcherInterface that reports
// devices from /sys.
class AmbientLightSensorWatcherUdev : public AmbientLightSensorWatcherInterface,
                                      public UdevSubsystemObserver {
 public:
  // Udev subsystem used to watch for ambient light sensor related changes.
  static const char kIioUdevSubsystem[];

  // Udev device type.
  static const char kIioUdevDevice[];

  AmbientLightSensorWatcherUdev();
  AmbientLightSensorWatcherUdev(const AmbientLightSensorWatcherUdev&) = delete;
  AmbientLightSensorWatcherUdev& operator=(
      const AmbientLightSensorWatcherUdev&) = delete;

  ~AmbientLightSensorWatcherUdev() override;

  // Ownership of |udev| remains with the caller.
  void Init(UdevInterface* udev);

  // UdevSubsystemObserver implementation:
  void OnUdevEvent(const UdevEvent& event) override;

 private:
  // Checks if the udev device is an ambient light sensor.
  bool IsAmbientLightSensor(const UdevDeviceInfo& device_info);

  // Called when a new udev device is connected. If it's an ambient light sensor
  // adds it to |ambient_light_sensors_| and notifies observers.
  void OnAddUdevDevice(const UdevDeviceInfo& device_info);

  // Called when a new udev device is disconnected. If it's an ambient light
  // sensor removes it from |ambient_light_sensors_| and notifies observers.
  void OnRemoveUdevDevice(const UdevDeviceInfo& device_info);

  UdevInterface* udev_;  // weak pointer
};

}  // namespace system
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_SYSTEM_AMBIENT_LIGHT_SENSOR_WATCHER_UDEV_H_
