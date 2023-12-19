// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_FLOSS_BATTERY_PROVIDER_H_
#define POWER_MANAGER_POWERD_SYSTEM_FLOSS_BATTERY_PROVIDER_H_

#include <string>

#include <brillo/dbus/exported_object_manager.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/message.h>
#include <dbus/object_manager.h>

#include "power_manager/powerd/system/bluetooth_battery_provider.h"

namespace power_manager::system {

// Represents Floss's battery provider for Human Interface Devices (HID). It
// manages the sending of battery data changes to the Floss daemon.
class FlossBatteryProvider : public BluetoothBatteryProvider {
 public:
  FlossBatteryProvider();

  // Initializes the provider.
  void Init(scoped_refptr<dbus::Bus> bus);

  // Resets the state like it was just init-ed.
  void Reset() override;

  // Notify the battery provider manager about a change in device battery level.
  void UpdateDeviceBattery(const std::string& address, int level) override;

 private:
  friend class FlossBatteryProviderTest;

  base::WeakPtrFactory<FlossBatteryProvider> weak_ptr_factory_;
};

}  // namespace power_manager::system

#endif  // POWER_MANAGER_POWERD_SYSTEM_FLOSS_BATTERY_PROVIDER_H_
