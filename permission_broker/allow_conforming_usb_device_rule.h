// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERMISSION_BROKER_ALLOW_CONFORMING_USB_DEVICE_RULE_H_
#define PERMISSION_BROKER_ALLOW_CONFORMING_USB_DEVICE_RULE_H_

#include <libudev.h>
#include <policy/device_policy.h>
#include <policy/libpolicy.h>

#include <optional>
#include <vector>

#include "permission_broker/rule.h"
#include "permission_broker/rule_utils.h"
#include "permission_broker/usb_subsystem_udev_rule.h"

namespace permission_broker {

/// AllowConformingUsbDeviceRule aims to control which USB devices are ALLOWed
/// to other contexts (Chrome, VM guests, etc)
///
/// The top-level stance relies on firmware (for x86)/kernel DT (for ARM)/custom
/// udev rules information to create udev properties, tagging ports as internal
/// or external. By and large, we want to take the stance that if a user has
/// expressly plugged a device in and chosen to share it with another context,
/// they should be allowed to do so. In cases where we cannot ascertain if a
/// device is internal or external, we fall back to the 'legacy' behavior of
/// allowing a few default cases (storage devices, adb, etc), and checking allow
/// lists and policies.
///
/// The remaining logic resolves around the ALLOW variant to be used:
/// ALLOW_WITH_DETACH
///  * In cases where we have reasonable certainty that a device is 'safe', we
///    permit it to be shared with its context after detaching host kernel
///    drivers. These cases roughly consist of: being a known external device,
///    or failing that, matching the legacy heuristics.
/// ALLOW_WITH_LOCKDOWN
///  * In the legacy workflow, if both claimed and unclaimed interfaces are
///    found we allow the device to be shared if the USBDEVFS_DROP_PRIVILEGES
///    ioctl is called, and we do not attempt to detach kernel drivers. NOTE:
///    callers to permission_broker may choose to pass a mask of interfaces to
///    mask, in which cases the above flow would be triggered regardless of
///    allow-variant returned by any rules.
class AllowConformingUsbDeviceRule : public UsbSubsystemUdevRule {
 public:
  AllowConformingUsbDeviceRule();
  AllowConformingUsbDeviceRule(const AllowConformingUsbDeviceRule&) = delete;
  AllowConformingUsbDeviceRule& operator=(const AllowConformingUsbDeviceRule&) =
      delete;

  ~AllowConformingUsbDeviceRule() override;

  Result ProcessUsbDevice(udev_device* device) override;

 protected:
  // Devices that have been allowed via device policy.
  std::vector<policy::DevicePolicy::UsbDeviceId> usb_allow_list_;

 private:
  // Device policy is cached after a successful load.
  bool policy_loaded_;

  // Loads the device settings policy and returns success.
  virtual bool LoadPolicy();

  // Returns whether a USB device is allowed inside the device settings
  // to be detached from its kernel driver.
  bool IsDeviceDetachableByPolicy(udev_device* device);

  Result ProcessLegacyDevice(
      udev_device* device,
      std::optional<CrosUsbLocationProperty> cros_usb_location);
  Result ProcessTaggedDevice(udev_device* device,
                             CrosUsbLocationProperty location);

  // If running on a chromebox, ignore external/internal tagging.
  const bool running_on_chromebox_;
};

}  // namespace permission_broker

#endif  // PERMISSION_BROKER_ALLOW_CONFORMING_USB_DEVICE_RULE_H_
