// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_UTILS_FWUPD_UTILS_H_
#define DIAGNOSTICS_CROS_HEALTHD_UTILS_FWUPD_UTILS_H_

#include <optional>
#include <string>
#include <vector>

#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace diagnostics {
namespace fwupd_utils {

// DeviceInfo stores the data of a fwupd device.
struct DeviceInfo {
  // The device name, e.g. "Type-C Video Adapter".
  std::optional<std::string> name;

  // The list of globally unique identifiers, e.g.
  // ["2082b5e0-7a64-478a-b1b2-e3404fab6dad"].
  std::vector<std::string> guids;

  // The list of device instance IDs, e.g. ["USB\VID_0A5C&PID_6412"].
  std::vector<std::string> instance_ids;

  // The device serial number, e.g. "0000084f2cb5".
  std::optional<std::string> serial;

  // The firmware version in string, e.g. "1.2.3", "v42".
  std::optional<std::string> version;

  // The format of device firmware version, e.g. PLAIN, HEX, BCD.
  chromeos::cros_healthd::mojom::FwupdVersionFormat version_format;

  // The device vendor IDs joined by '|', e.g. "USB:0x1234|PCI:0x5678".
  std::optional<std::string> joined_vendor_id;
};

// Returns whether |device_info| contains a specific |vendor_id|, e.g.
// "USB:0x1234".
bool ContainsVendorId(const DeviceInfo& device_info,
                      const std::string& vendor_id);

// Returns the device GUID generated from the instance ID or NULL if the
// conversion fails.
std::optional<std::string> InstanceIdToGuid(const std::string& instance_id);

}  // namespace fwupd_utils
}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_UTILS_FWUPD_UTILS_H_
