// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_POLICY_BLUETOOTH_CONTROLLER_H_
#define POWER_MANAGER_POWERD_POLICY_BLUETOOTH_CONTROLLER_H_

#include <base/containers/flat_map.h>
#include <base/files/file_path.h>

#include "power_manager/powerd/system/udev.h"
#include "power_manager/powerd/system/udev_subsystem_observer.h"

namespace power_manager {
namespace policy {

// BluetoothController initiates power-related changes to the Bluetooth chipset.
class BluetoothController : public system::UdevSubsystemObserver {
 public:
  // Bluetooth subsystem and host devtype for udev events.
  static const char kUdevSubsystem[];
  static const char kUdevDevtype[];

  // Runtime suspend control and enabled/disabled constants.
  static const char kAutosuspendSysattr[];
  static const char kAutosuspendEnabled[];
  static const char kAutosuspendDisabled[];

  BluetoothController();
  BluetoothController(const BluetoothController&) = delete;
  BluetoothController& operator=(const BluetoothController&) = delete;

  ~BluetoothController() override;

  void Init(system::UdevInterface* udev);

  // Bluetooth devices currently have a quirk where suspending while
  // autosuspended can cause events to increment the wake count while
  // suspending. To get around this, we disable autosuspend before suspending
  // and re-enable it after suspend.
  void ApplyAutosuspendQuirk();

  // Unapply the autosuspend quirk.
  void UnapplyAutosuspendQuirk();

  // system::UdevSubsystemObserver
  void OnUdevEvent(const system::UdevEvent& event) override;

 private:
  system::UdevInterface* udev_ = nullptr;  // Not owned.

  // Map of known Bluetooth devices and their power/control path.
  base::flat_map<base::FilePath, base::FilePath> bt_hosts_;
};

}  // namespace policy
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_POLICY_BLUETOOTH_CONTROLLER_H_
