// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "typecd/utils.h"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <string>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>

#include "typecd/metrics_allowlist.h"

namespace typecd {

bool ReadHexFromPath(const base::FilePath& path, uint32_t* val) {
  std::string val_str;
  if (!base::ReadFileToString(path, &val_str)) {
    LOG(ERROR) << "Couldn't read value from path " << path;
    return false;
  }
  base::TrimWhitespaceASCII(val_str, base::TRIM_TRAILING, &val_str);

  if (!base::HexStringToUInt(val_str.c_str(), val)) {
    LOG(ERROR) << "Error parsing hex value: " << val_str;
    return false;
  }

  return true;
}

std::string FormatHexString(uint32_t val, int width) {
  std::stringstream out;
  out << std::hex << std::setfill('0') << std::setw(width) << val;
  return out.str();
}

bool DeviceComp(policy::DevicePolicy::UsbDeviceId dev1,
                policy::DevicePolicy::UsbDeviceId dev2) {
  // Allowlist entries are first sorted by VID.
  if (dev1.vendor_id < dev2.vendor_id)
    return true;
  else if (dev1.vendor_id > dev2.vendor_id)
    return false;

  // If 2 entries have the same VID, they are sorted by PID.
  return (dev1.product_id < dev2.product_id);
}

bool DeviceInMetricsAllowlist(uint16_t vendor_id, uint16_t product_id) {
  policy::DevicePolicy::UsbDeviceId device = {vendor_id, product_id};
  return std::binary_search(std::begin(kMetricsAllowlist),
                            std::end(kMetricsAllowlist), device, DeviceComp);
}

}  // namespace typecd
