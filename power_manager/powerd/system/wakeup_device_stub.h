// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_WAKEUP_DEVICE_STUB_H_
#define POWER_MANAGER_POWERD_SYSTEM_WAKEUP_DEVICE_STUB_H_

#include "power_manager/powerd/system/wakeup_device_interface.h"

#include <memory>
#include <set>

namespace power_manager {
namespace system {

class UdevInterface;

class WakeupDeviceStub : public WakeupDeviceInterface {
 public:
  WakeupDeviceStub();
  ~WakeupDeviceStub() override;

  void set_caused_last_wake(bool caused_last_wake) {
    caused_last_wake_ = caused_last_wake;
  }

  // Implementation of WakeupDeviceInterface.
  void PrepareForSuspend() override;
  void HandleResume() override;
  bool CausedLastWake() const override;

 private:
  // Did the device cause the last wake ?
  bool caused_last_wake_ = false;

  DISALLOW_COPY_AND_ASSIGN(WakeupDeviceStub);
};

class WakeupDeviceFactoryStub : public WakeupDeviceFactoryInterface {
 public:
  WakeupDeviceFactoryStub();
  ~WakeupDeviceFactoryStub() override;

  // Checks whether |CreateWakeupDevice| was called for a given sysfs_path.
  bool WasDeviceCreated(const base::FilePath& sysfs_path) const;

  // Implementation of WakeupDeviceFactoryInterface.
  std::unique_ptr<WakeupDeviceInterface> CreateWakeupDevice(
      const base::FilePath& sysfs_path) override;

 private:
  // Set of sysfs paths for which |CreateWakeupDevice| was called.
  std::set<base::FilePath> registered_wakeup_device_paths_;

  DISALLOW_COPY_AND_ASSIGN(WakeupDeviceFactoryStub);
};

}  // namespace system
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_SYSTEM_WAKEUP_DEVICE_STUB_H_
