// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/utils/usb_utils.h"

#include <base/files/file_path.h>

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
}  // namespace

std::string GetUsbVendorName(
    const std::unique_ptr<brillo::UdevDevice>& device) {
  const char* prop = device->GetPropertyValue(kPropertieVendor);
  if (prop)
    return prop;
  std::string vendor;
  ReadAndTrimString(GetSysPath(device), kFileUsbManufacturerName, &vendor);
  return vendor;
}

std::string GetUsbProductName(
    const std::unique_ptr<brillo::UdevDevice>& device) {
  const char* prop = device->GetPropertyValue(kPropertieProduct);
  if (prop)
    return prop;
  std::string product;
  ReadAndTrimString(GetSysPath(device), kFileUsbProductName, &product);
  return product;
}

}  // namespace diagnostics
