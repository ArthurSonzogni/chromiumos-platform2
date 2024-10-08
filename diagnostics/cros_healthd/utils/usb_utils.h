// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_UTILS_USB_UTILS_H_
#define DIAGNOSTICS_CROS_HEALTHD_UTILS_USB_UTILS_H_

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "diagnostics/mojom/public/cros_healthd_probe.mojom-forward.h"

namespace base {
class FilePath;
}  // namespace base

namespace brillo {
class UdevDevice;
}  // namespace brillo

namespace diagnostics {

// Returns vendor name of a usb device. It uses udev to query the `usb.ids` file
// and fallback to sysfs if doesn't find.
std::string GetUsbVendorName(const std::unique_ptr<brillo::UdevDevice>& device);
// Returns vendor name of a usb device.
std::string GetUsbVendorName(const base::FilePath& sys_path);
// Returns product name of a usb device. Similar to `GetUsbVendorName()`.
std::string GetUsbProductName(
    const std::unique_ptr<brillo::UdevDevice>& device);
// Returns product name of a usb device.
std::string GetUsbProductName(const base::FilePath& sys_path);
// Returns vid and pid of a usb device.
std::pair<uint16_t, uint16_t> GetUsbVidPid(
    const std::unique_ptr<brillo::UdevDevice>& device);
// Returns human readable device class string.
std::string LookUpUsbDeviceClass(const int class_code);
// Determine usb protocol version by checking the root hub version.
ash::cros_healthd::mojom::UsbVersion DetermineUsbVersion(
    const base::FilePath& sysfs_path);
// Returns usb spec speed.
ash::cros_healthd::mojom::UsbSpecSpeed GetUsbSpecSpeed(
    const base::FilePath& sysfs_path);

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_UTILS_USB_UTILS_H_
