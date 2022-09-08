// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/policy/bluetooth_controller.h"

#include <string>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/check_op.h>
#include <base/files/file_util.h>
#include <base/logging.h>

namespace power_manager {
namespace policy {
namespace {

// Check if the `power/control` path exists at the wakeup path and return the
// path to the power/control path if it does.
base::FilePath GetControlPathFromDeviceInfo(
    const system::UdevDeviceInfo& info) {
  base::FilePath control_path =
      base::FilePath(info.wakeup_device_path)
          .Append(BluetoothController::kAutosuspendSysattr);

  if (!base::PathExists(control_path)) {
    return base::FilePath();
  }

  if (!base::PathIsReadable(control_path) ||
      !base::PathIsWritable(control_path)) {
    LOG(ERROR) << "Bluetooth device power-control is not accessible to powerd: "
               << control_path << ", syspath=" << info.syspath;
  }

  return control_path;
}
}  // namespace

const char BluetoothController::kUdevSubsystem[] = "bluetooth";
const char BluetoothController::kUdevDevtype[] = "host";

// See https://www.kernel.org/doc/Documentation/ABI/testing/sysfs-devices-power
const char BluetoothController::kAutosuspendSysattr[] = "power/control";
const char BluetoothController::kAutosuspendEnabled[] = "auto";
const char BluetoothController::kAutosuspendDisabled[] = "on";

BluetoothController::BluetoothController() = default;
BluetoothController::~BluetoothController() {
  if (udev_)
    udev_->RemoveSubsystemObserver(kUdevSubsystem, this);
}

void BluetoothController::Init(system::UdevInterface* udev) {
  DCHECK(udev);

  udev_ = udev;
  udev_->AddSubsystemObserver(kUdevSubsystem, this);

  // List all initial entries in Bluetooth subsystem
  bt_hosts_.clear();
  std::vector<system::UdevDeviceInfo> found;
  if (udev_->GetSubsystemDevices(kUdevSubsystem, &found)) {
    for (const system::UdevDeviceInfo& dev : found) {
      if (dev.devtype != kUdevDevtype) {
        continue;
      }
      base::FilePath control_path = GetControlPathFromDeviceInfo(dev);
      bt_hosts_.emplace(std::make_pair(dev.syspath, control_path));
    }
  }
}

void BluetoothController::ApplyAutosuspendQuirk() {
  std::string disable(kAutosuspendDisabled);

  for (const auto& device : bt_hosts_) {
    // If the host device has a power/control sysattr, disable autosuspend
    // before we enter suspend.
    if (device.second != base::FilePath()) {
      bool success =
          base::WriteFile(device.second, disable.data(), disable.size());
      LOG(INFO) << "Writing \"" << disable << "\" to " << device.second << " "
                << (success ? "succeeded" : "failed");
    }
  }
}

void BluetoothController::UnapplyAutosuspendQuirk() {
  std::string enable(kAutosuspendEnabled);

  for (const auto& device : bt_hosts_) {
    if (device.second != base::FilePath()) {
      bool success =
          base::WriteFile(device.second, enable.data(), enable.size());
      LOG(INFO) << "Writing \"" << enable << "\" to " << device.second << " "
                << (success ? "succeeded" : "failed");
    }
  }
}

void BluetoothController::OnUdevEvent(const system::UdevEvent& event) {
  DCHECK_EQ(event.device_info.subsystem, kUdevSubsystem);
  if (event.device_info.devtype != kUdevDevtype)
    return;

  base::FilePath control_path;

  // Update the power/control path when bluetooth hosts are added or removed.
  switch (event.action) {
    case system::UdevEvent::Action::ADD:
    case system::UdevEvent::Action::CHANGE:
      control_path = GetControlPathFromDeviceInfo(event.device_info);
      bt_hosts_.emplace(
          std::make_pair(event.device_info.syspath, control_path));
      break;

    case system::UdevEvent::Action::REMOVE:
      bt_hosts_.erase(base::FilePath(event.device_info.syspath));
      break;

    default:
      break;
  }
}

}  // namespace policy
}  // namespace power_manager
