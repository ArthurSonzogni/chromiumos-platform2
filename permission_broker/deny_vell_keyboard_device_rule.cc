// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "permission_broker/deny_vell_keyboard_device_rule.h"

#include <libudev.h>

#include <base/strings/string_number_conversions.h>

namespace permission_broker {

namespace {

constexpr uint32_t kVellKbdVid = 0x18d1;
constexpr uint32_t kVellKbdPid = 0x5022;

bool GetUIntSysattr(udev_device* device, const char* key, uint32_t* val) {
  CHECK(val);

  const char* str_val = udev_device_get_sysattr_value(device, key);
  return str_val && base::HexStringToUInt(str_val, val);
}

}  // namespace

DenyVellKeyboardDeviceRule::DenyVellKeyboardDeviceRule()
    : UsbSubsystemUdevRule("DenyVellKeyboardDeviceRule") {}

Rule::Result DenyVellKeyboardDeviceRule::ProcessUsbDevice(
    struct udev_device* device) {
  uint32_t vendor_id, product_id;
  GetUIntSysattr(device, "idVendor", &vendor_id);
  GetUIntSysattr(device, "idProduct", &product_id);
  if (vendor_id == kVellKbdVid && product_id == kVellKbdPid) {
    return DENY;
  }

  // Not Vell's keyboard, pass through IGNORE.
  return IGNORE;
}

}  // namespace permission_broker
