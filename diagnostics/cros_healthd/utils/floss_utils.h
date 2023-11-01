// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_UTILS_FLOSS_UTILS_H_
#define DIAGNOSTICS_CROS_HEALTHD_UTILS_FLOSS_UTILS_H_

#include <optional>
#include <string>
#include <vector>

#include <base/uuid.h>
#include <brillo/variant_dictionary.h>

namespace diagnostics::floss_utils {

// Parse and convert 128 bits UUID to the base::Uuid object. If any error
// happens, return an invalid base::Uuid object.
base::Uuid ParseUuidBytes(const std::vector<uint8_t>& bytes);

// The info of device from Floss.
struct DeviceInfo {
  std::string address;
  std::string name;
};

// Return the result of parsing the device dictionary from Floss.
std::optional<DeviceInfo> ParseDeviceInfo(
    const brillo::VariantDictionary& device);

}  // namespace diagnostics::floss_utils

#endif  // DIAGNOSTICS_CROS_HEALTHD_UTILS_FLOSS_UTILS_H_
