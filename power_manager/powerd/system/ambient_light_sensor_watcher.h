// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_AMBIENT_LIGHT_SENSOR_WATCHER_H_
#define POWER_MANAGER_POWERD_SYSTEM_AMBIENT_LIGHT_SENSOR_WATCHER_H_

#include <vector>

#include <base/observer_list.h>

#include "power_manager/powerd/system/ambient_light_sensor_info.h"
#include "power_manager/powerd/system/ambient_light_sensor_watcher_interface.h"
#include "power_manager/powerd/system/ambient_light_sensor_watcher_observer.h"
#include "power_manager/powerd/system/udev_subsystem_observer.h"

namespace power_manager {
namespace system {

// Real implementation of AmbientLightSensorWatcherInterface that reports
// devices from /sys.
class AmbientLightSensorWatcher : public AmbientLightSensorWatcherInterface,
                                  public UdevSubsystemObserver {
 public:
  // Udev subsystem used to watch for ambient light sensor related changes.
  static const char kIioUdevSubsystem[];

  // Udev device type.
  static const char kIioUdevDevice[];

  AmbientLightSensorWatcher();
  AmbientLightSensorWatcher(const AmbientLightSensorWatcher&) = delete;
  AmbientLightSensorWatcher& operator=(const AmbientLightSensorWatcher&) =
      delete;

  ~AmbientLightSensorWatcher() override;

  // Ownership of |udev| remains with the caller.
  void Init(UdevInterface* udev);

  // AmbientLightSensorWatcherInterface implementation:
  const std::vector<AmbientLightSensorInfo>& GetAmbientLightSensors()
      const override;
  void AddObserver(AmbientLightSensorWatcherObserver* observer) override;
  void RemoveObserver(AmbientLightSensorWatcherObserver* observer) override;

  // UdevSubsystemObserver implementation:
  void OnUdevEvent(const UdevEvent& event) override;

 private:
  // Called when changes are made to |ambient_light_sensors_| to notify
  // observers.
  void NotifyObservers();

  // Checks if the udev device is an ambient light sensor.
  bool IsAmbientLightSensor(const UdevDeviceInfo& device_info);

  // Called when a new udev device is connected. If it's an ambient light sensor
  // adds it to |ambient_light_sensors_| and notifies observers.
  void OnAddUdevDevice(const UdevDeviceInfo& device_info);

  // Called when a new udev device is disconnected. If it's an ambient light
  // sensor removes it from |ambient_light_sensors_| and notifies observers.
  void OnRemoveUdevDevice(const UdevDeviceInfo& device_info);

  UdevInterface* udev_;  // weak pointer

  base::ObserverList<AmbientLightSensorWatcherObserver> observers_;

  // Currently-connected ambient light sensors.
  std::vector<AmbientLightSensorInfo> ambient_light_sensors_;
};

}  // namespace system
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_SYSTEM_AMBIENT_LIGHT_SENSOR_WATCHER_H_
