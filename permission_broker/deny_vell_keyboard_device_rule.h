// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERMISSION_BROKER_DENY_VELL_KEYBOARD_DEVICE_RULE_H_
#define PERMISSION_BROKER_DENY_VELL_KEYBOARD_DEVICE_RULE_H_

#include "permission_broker/usb_subsystem_udev_rule.h"

namespace permission_broker {

// The Vell keyboard is not a true USB device that can be interacted with. This
// rule denies path access to the device.
class DenyVellKeyboardDeviceRule : public UsbSubsystemUdevRule {
 public:
  DenyVellKeyboardDeviceRule();
  DenyVellKeyboardDeviceRule(const DenyVellKeyboardDeviceRule&) = delete;
  DenyVellKeyboardDeviceRule& operator=(const DenyVellKeyboardDeviceRule&) =
      delete;

  ~DenyVellKeyboardDeviceRule() override = default;

  Result ProcessUsbDevice(struct udev_device* device) override;
};

}  // namespace permission_broker

#endif  // PERMISSION_BROKER_DENY_VELL_KEYBOARD_DEVICE_RULE_H_
