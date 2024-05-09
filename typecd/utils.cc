// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "typecd/utils.h"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <string>

#include <base/files/file_enumerator.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/stringprintf.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <re2/re2.h>

#include "typecd/metrics_allowlist.h"

namespace {

constexpr char kTbtDeviceRegex[] = "[0-9]+\\-[0-9]+";
constexpr char kTbtDeviceDir[] = "sys/bus/thunderbolt/devices";
constexpr char kBusnum[] = "busnum";
constexpr char kDevnum[] = "devnum";
constexpr char kDuration[] = "power/connected_duration";

}  // namespace

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

int GetTbtDeviceCount() {
  int ret = 0;
  base::FileEnumerator tbt_links(
      base::FilePath(kTbtDeviceDir), false,
      base::FileEnumerator::FILES | base::FileEnumerator::SHOW_SYM_LINKS);
  for (base::FilePath tbt_link = tbt_links.Next(); !tbt_link.empty();
       tbt_link = tbt_links.Next()) {
    if (RE2::FullMatch(tbt_link.BaseName().value(), kTbtDeviceRegex))
      ret++;
  }

  return ret;
}

int ReadUsbProp(base::FilePath usb_device, std::string prop) {
  int ret;
  std::string prop_str;
  if (base::ReadFileToString(usb_device.Append(prop), &prop_str)) {
    base::TrimWhitespaceASCII(prop_str, base::TRIM_ALL, &prop_str);
    if (base::StringToInt(prop_str, &ret)) {
      return ret;
    }
  }

  return 0;
}

std::string GetConnectionId(std::string boot_id, base::FilePath usb_device) {
  struct timespec ts;
  int64_t duration, connect_time;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  duration = static_cast<int64_t>(ReadUsbProp(usb_device, kDuration) / 1000);
  connect_time = (ts.tv_sec - duration) / 60;

  return base::StringPrintf(
      "%s.%s.%s.%s", boot_id.c_str(), std::to_string(connect_time).c_str(),
      std::to_string(ReadUsbProp(usb_device, kBusnum)).c_str(),
      std::to_string(ReadUsbProp(usb_device, kDevnum)).c_str());
}

}  // namespace typecd
