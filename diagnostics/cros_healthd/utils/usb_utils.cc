// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <libusb.h>

#include "diagnostics/cros_healthd/utils/usb_utils.h"

#include <base/files/file_path.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>

#include "diagnostics/cros_healthd/utils/error_utils.h"
#include "diagnostics/cros_healthd/utils/file_utils.h"
#include "diagnostics/cros_healthd/utils/usb_utils_constants.h"

namespace diagnostics {
namespace {
base::FilePath GetSysPath(const std::unique_ptr<brillo::UdevDevice>& device) {
  const char* syspath = device->GetSysPath();
  DCHECK(syspath);
  // Return root. It is safe to read a non-exist file.
  return base::FilePath(syspath ? syspath : "/");
}

std::pair<std::string, std::string> GetUsbVidPidFromSys(
    const std::unique_ptr<brillo::UdevDevice>& device) {
  std::string vid;
  std::string pid;
  const auto sys_path = GetSysPath(device);

  ReadAndTrimString(sys_path, kFileUsbVendor, &vid);
  ReadAndTrimString(sys_path, kFileUsbProduct, &pid);
  return std::make_pair(vid, pid);
}
}  // namespace

std::string GetUsbVendorName(
    const std::unique_ptr<brillo::UdevDevice>& device) {
  const char* prop = device->GetPropertyValue(kPropertieVendorFromDB);
  if (prop)
    return prop;
  std::string vendor;
  ReadAndTrimString(GetSysPath(device), kFileUsbManufacturerName, &vendor);
  return vendor;
}

std::string GetUsbProductName(
    const std::unique_ptr<brillo::UdevDevice>& device) {
  const char* prop = device->GetPropertyValue(kPropertieModelFromDB);
  if (prop)
    return prop;
  std::string product;
  ReadAndTrimString(GetSysPath(device), kFileUsbProductName, &product);
  return product;
}

std::pair<uint16_t, uint16_t> GetUsbVidPid(
    const std::unique_ptr<brillo::UdevDevice>& device) {
  std::string raw_vid;
  std::string raw_pid;
  // We should read info from |PRODUCT| first, which is provided by udev event.
  // Because sysfs is unavailable during a remove event.
  // PRODUCT format: {VID}/{PID}/{bcdDevice}
  const char* product = device->GetPropertyValue(kPropertieProduct);
  if (product) {
    auto product_tokens =
        base::SplitString(std::string(product), "/", base::TRIM_WHITESPACE,
                          base::SPLIT_WANT_NONEMPTY);
    if (product_tokens.size() == 3) {
      raw_vid = product_tokens[0];
      raw_pid = product_tokens[1];
    }
  }

  if (raw_vid == "" || raw_pid == "")
    std::tie(raw_vid, raw_pid) = GetUsbVidPidFromSys(device);

  uint32_t vid = 0;
  uint32_t pid = 0;
  if (!base::HexStringToUInt(raw_vid, &vid) ||
      !base::HexStringToUInt(raw_pid, &pid)) {
    vid = pid = 0;
    LOG(ERROR) << "Can't convert hex string for vid: " << raw_vid
               << ", and pid: " << raw_pid;
  }

  return std::make_pair(static_cast<uint16_t>(vid), static_cast<uint16_t>(pid));
}

std::string LookUpUsbDeviceClass(const int class_code) {
  // https://www.usb.org/defined-class-codes
  switch (class_code) {
    case libusb_class_code::LIBUSB_CLASS_AUDIO:
      return "Audio";
    case libusb_class_code::LIBUSB_CLASS_COMM:
      return "Communication";
    case libusb_class_code::LIBUSB_CLASS_HID:
      return "Human Interface Device";
    case libusb_class_code::LIBUSB_CLASS_PHYSICAL:
      return "Physical";
    case libusb_class_code::LIBUSB_CLASS_PRINTER:
      return "Printer";
    case libusb_class_code::LIBUSB_CLASS_IMAGE:
      return "Image";
    case libusb_class_code::LIBUSB_CLASS_MASS_STORAGE:
      return "Mass storage";
    case libusb_class_code::LIBUSB_CLASS_HUB:
      return "Hub";
    case libusb_class_code::LIBUSB_CLASS_DATA:
      return "Data";
    case libusb_class_code::LIBUSB_CLASS_SMART_CARD:
      return "Smart Card";
    case libusb_class_code::LIBUSB_CLASS_CONTENT_SECURITY:
      return "Content Security";
    case libusb_class_code::LIBUSB_CLASS_VIDEO:
      return "Video";
    case libusb_class_code::LIBUSB_CLASS_PERSONAL_HEALTHCARE:
      return "Personal Healthcare";
    case libusb_class_code::LIBUSB_CLASS_DIAGNOSTIC_DEVICE:
      return "Diagnostic Device";
    case libusb_class_code::LIBUSB_CLASS_WIRELESS:
      return "Wireless";
    case libusb_class_code::LIBUSB_CLASS_APPLICATION:
      return "Application";
    case libusb_class_code::LIBUSB_CLASS_VENDOR_SPEC:
      return "Vendor Specific";
    case libusb_class_code::LIBUSB_CLASS_PER_INTERFACE:
      // Return "Unknown" because it means that the category is defined by its
      // interfaces.
      return "Unknown";
    default:
      return "Unknown";
  }
}

}  // namespace diagnostics
