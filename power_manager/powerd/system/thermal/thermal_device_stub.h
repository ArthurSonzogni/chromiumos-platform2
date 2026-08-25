// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_THERMAL_THERMAL_DEVICE_STUB_H_
#define POWER_MANAGER_POWERD_SYSTEM_THERMAL_THERMAL_DEVICE_STUB_H_

#include <string>

#include <base/observer_list.h>

#include "power_manager/powerd/system/thermal/device_thermal_state.h"
#include "power_manager/powerd/system/thermal/thermal_device.h"
#include "power_manager/powerd/system/thermal/thermal_device_observer.h"

namespace power_manager::system {

class ThermalDeviceStub : public ThermalDeviceInterface {
 public:
  ThermalDeviceStub() = default;
  ThermalDeviceStub(const ThermalDeviceStub&) = delete;
  ThermalDeviceStub& operator=(const ThermalDeviceStub&) = delete;

  ~ThermalDeviceStub() override = default;

  // ThermalDeviceInterface implementation:
  void AddObserver(ThermalDeviceObserver* observer) override;
  void RemoveObserver(ThermalDeviceObserver* observer) override;
  DeviceThermalState GetThermalState() const override;
  ThermalDeviceType GetType() const override;
  double GetWeight() const override;
  double GetThrottleRatio() const override;

  // Sets new thermal state, do not notify observers.
  void set_thermal_state(DeviceThermalState new_state) {
    current_state_ = new_state;
  }

  void set_type(ThermalDeviceType new_type) { type_ = new_type; }

  void set_weight(double weight) { weight_ = weight; }
  void set_throttle_ratio(double ratio) { throttle_ratio_ = ratio; }

  // Notifies |observers_| for thermal state change.
  void NotifyObservers();

 private:
  // List of observers that are currently interested in updates from this.
  base::ObserverList<ThermalDeviceObserver> observers_;

  DeviceThermalState current_state_ = DeviceThermalState::kUnknown;

  ThermalDeviceType type_ = ThermalDeviceType::kUnknown;

  double weight_ = 1.0;

  double throttle_ratio_ = 0.0;
};

}  // namespace power_manager::system

#endif  // POWER_MANAGER_POWERD_SYSTEM_THERMAL_THERMAL_DEVICE_STUB_H_
