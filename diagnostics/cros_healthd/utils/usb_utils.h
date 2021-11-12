// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_UTILS_USB_UTILS_H_
#define DIAGNOSTICS_CROS_HEALTHD_UTILS_USB_UTILS_H_

#include <memory>
#include <string>

#include <brillo/udev/udev_device.h>

namespace diagnostics {

// Returns vendor name of a usb device. It uses udev to query the `usb.ids` file
// and fallback to sysfs if doesn't find.
std::string GetUsbVendorName(const std::unique_ptr<brillo::UdevDevice>& device);
// Returns product name of a usb device. Similar to the above method.
std::string GetUsbProductName(
    const std::unique_ptr<brillo::UdevDevice>& device);

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_UTILS_USB_UTILS_H_
