// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_BLUETOOTH_BATTERY_PROVIDER_H_
#define POWER_MANAGER_POWERD_SYSTEM_BLUETOOTH_BATTERY_PROVIDER_H_

#include <memory>
#include <string>

namespace power_manager::system {

// An interface for interacting with bluetooth battery providers.
class BluetoothBatteryProvider {
 public:
  virtual ~BluetoothBatteryProvider() = default;

  virtual void Reset() = 0;

  virtual void UpdateDeviceBattery(const std::string& address, int level) = 0;
};

}  // namespace power_manager::system

#endif  // POWER_MANAGER_POWERD_SYSTEM_BLUETOOTH_BATTERY_PROVIDER_H_
