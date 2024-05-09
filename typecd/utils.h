// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TYPECD_UTILS_H_
#define TYPECD_UTILS_H_

#include <string>

#include <base/files/file_path.h>
#include <policy/device_policy.h>

namespace typecd {

// Helper function to parse Hex values from sysfs file paths.
// On success, the argument |*val| is updated with the value read.
//
// Returns:
//   True on success, False otherwise.
bool ReadHexFromPath(const base::FilePath& path, uint32_t* val);

// Helper function to create a string to print a value in hexidecimal. The
// string returned by FormatHexString will be zero-padded up to the provided
// width.
std::string FormatHexString(uint32_t val, int width);

// Helper functions used to search through metric allow list.
bool DeviceComp(policy::DevicePolicy::UsbDeviceId dev1,
                policy::DevicePolicy::UsbDeviceId dev2);
bool DeviceInMetricsAllowlist(uint16_t vendor_id, uint16_t product_id);

// Helper function to return the number of currently enumertated devices on the
// thunderbolt bus. Used to check for successful TBT/USB4 mode entry.
int GetTbtDeviceCount();

// Helper function to read a device property from |usb_device|.
int ReadUsbProp(base::FilePath usb_device, std::string prop);

// Given a boot_id and USB device path, generates a unique connection ID based
// boot_id, enumeration time, busnum and devnum.
std::string GetConnectionId(std::string boot_id, base::FilePath usb_device);

}  // namespace typecd

#endif  // TYPECD_UTILS_H_
