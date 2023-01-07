// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "permission_broker/rule_utils.h"

#include <vector>

#include "base/strings/string_number_conversions.h"

namespace permission_broker {

bool GetUIntSysattr(udev_device* device, const char* key, uint32_t* val) {
  CHECK(val);
  const char* str_val = udev_device_get_sysattr_value(device, key);
  return str_val && base::HexStringToUInt(str_val, val);
}

template <typename Iterator>
bool UsbDeviceListContainsId(Iterator first,
                             Iterator last,
                             uint16_t vendor_id,
                             uint16_t product_id) {
  while (first != last) {
    if (first->vendor_id == vendor_id &&
        (!first->product_id || first->product_id == product_id))
      return true;
    ++first;
  }
  return false;
}

bool IsDeviceAllowedWebHID(udev_device* device) {
  const DevicePolicy::UsbDeviceId kWebHIDAllowedIds[] = {
      {0x0c27, 0x3bfa},  // rf IDEAS reader
      {0x0c27, 0x3b1e},  // rf IDEAS reader
      {0x0c27, 0xccda},  // rf IDEAS reader
      {0x0c27, 0xccdb},  // rf IDEAS reader
  };

  uint32_t vendor_id, product_id;
  if (!GetUIntSysattr(device, "idVendor", &vendor_id) ||
      !GetUIntSysattr(device, "idProduct", &product_id)) {
    return false;
  }

  return UsbDeviceListContainsId(std::begin(kWebHIDAllowedIds),
                                 std::end(kWebHIDAllowedIds), vendor_id,
                                 product_id);
}

template bool UsbDeviceListContainsId<const policy::DevicePolicy::UsbDeviceId*>(
    const policy::DevicePolicy::UsbDeviceId* first,
    const policy::DevicePolicy::UsbDeviceId* last,
    uint16_t vendor_id,
    uint16_t product_id);
template bool UsbDeviceListContainsId<
    std::vector<policy::DevicePolicy::UsbDeviceId>::iterator>(
    std::vector<policy::DevicePolicy::UsbDeviceId>::iterator first,
    std::vector<policy::DevicePolicy::UsbDeviceId>::iterator last,
    uint16_t vendor_id,
    uint16_t product_id);

}  // namespace permission_broker
