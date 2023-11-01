// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/utils/floss_utils.h"

#include <optional>
#include <string>

#include <base/logging.h>
#include <base/strings/strcat.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/uuid.h>
#include <brillo/variant_dictionary.h>

namespace diagnostics {

namespace {

// Return hexadecimal lowercase characters of `bytes` in index [`start`, `end`).
std::string BytesToHex(const std::vector<uint8_t>& bytes, int start, int end) {
  CHECK(end <= bytes.size());
  std::string out;
  for (int i = start; i < end; ++i) {
    base::StrAppend(&out, {base::StringPrintf("%02x", bytes[i])});
  }
  return out;
}

}  // namespace

namespace floss_utils {

base::Uuid ParseUuidBytes(const std::vector<uint8_t>& bytes) {
  // UUID should be 128 bits.
  if (bytes.size() != 16) {
    LOG(ERROR) << "Get invalid UUID bytes, size: " << bytes.size();
    return base::Uuid();
  }
  // Convert these bytes to the string of 32 hexadecimal lowercase characters in
  // the 8-4-4-4-12 format: XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX.
  return base::Uuid::ParseLowercase(
      base::JoinString({BytesToHex(bytes, 0, 4), BytesToHex(bytes, 4, 6),
                        BytesToHex(bytes, 6, 8), BytesToHex(bytes, 8, 10),
                        BytesToHex(bytes, 10, 16)},
                       /*separator=*/"-"));
}

std::optional<DeviceInfo> ParseDeviceInfo(
    const brillo::VariantDictionary& device) {
  // According to |BluetoothDeviceDBus| struct in the Android codebase:
  // packages/modules/Bluetooth/system/gd/rust/topshim/src/iface_bluetooth.rs,
  // a valid device dictionary should contain "address" and "name" keys.
  if (!device.contains("address") || !device.contains("name")) {
    return std::nullopt;
  }
  return DeviceInfo{
      .address =
          brillo::GetVariantValueOrDefault<std::string>(device, "address"),
      .name = brillo::GetVariantValueOrDefault<std::string>(device, "name")};
}

}  // namespace floss_utils

}  // namespace diagnostics
