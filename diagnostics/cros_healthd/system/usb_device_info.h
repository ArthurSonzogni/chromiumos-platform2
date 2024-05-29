// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_SYSTEM_USB_DEVICE_INFO_H_
#define DIAGNOSTICS_CROS_HEALTHD_SYSTEM_USB_DEVICE_INFO_H_

#include <map>
#include <string>

#include <base/files/file_path.h>
#include <gtest/gtest_prod.h>

namespace diagnostics {

enum DeviceType {
  kSD,
  kMobile,
  kUSB,
};

constexpr char kRelativeUSBDeviceInfoFile[] =
    "usr/share/cros-disks/usb-device-info";

// A class for querying information from a USB device info file.
class USBDeviceInfo {
 public:
  USBDeviceInfo();
  USBDeviceInfo(const USBDeviceInfo&) = delete;
  USBDeviceInfo& operator=(const USBDeviceInfo&) = delete;

  ~USBDeviceInfo();

  // Returns the device media type of a USB device with |vendor_id| and
  // |product_id|.
  DeviceType GetDeviceMediaType(const std::string& vendor_id,
                                const std::string& product_id) const;

  // Sets the internal map, should only be used in testing.
  void SetEntriesForTesting(std::map<std::string, DeviceType> entries);

 private:
  // Retrieves the list of USB device info from a file at |path|.
  // Returns true on success.
  void RetrieveFromFile(const base::FilePath& path);

  // Converts from string to enum of a device media type.
  DeviceType ConvertToDeviceMediaType(const std::string& str) const;

  // Returns true if |line| is skippable, i.e. an empty or comment line.
  bool IsLineSkippable(const std::string& line) const;

  // A map from an ID string, in form of <vendor id>:<product id>, to its device
  // type.
  std::map<std::string, DeviceType> entries_;

  FRIEND_TEST(USBDeviceInfoTest, ConvertToDeviceMediaType);
  FRIEND_TEST(USBDeviceInfoTest, IsLineSkippable);
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_SYSTEM_USB_DEVICE_INFO_H_
