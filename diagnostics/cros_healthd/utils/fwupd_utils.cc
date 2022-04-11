// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include <base/strings/string_split.h>
#include <libfwupd/fwupd-common.h>

#include "diagnostics/cros_healthd/utils/fwupd_utils.h"

namespace diagnostics {
namespace fwupd_utils {

bool ContainsVendorId(const DeviceInfo& device_info,
                      const std::string& vendor_id) {
  if (!device_info.joined_vendor_id.has_value()) {
    return false;
  }
  std::vector<base::StringPiece> ids =
      base::SplitStringPiece(device_info.joined_vendor_id.value(), "|",
                             base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  return std::any_of(
      ids.begin(), ids.end(),
      [&vendor_id](base::StringPiece value) { return value == vendor_id; });
}

std::optional<std::string> InstanceIdToGuid(const std::string& instance_id) {
  g_autofree gchar* guid_c_str = fwupd_guid_hash_string(instance_id.c_str());
  if (!guid_c_str) {
    return std::nullopt;
  }
  return std::string(guid_c_str);
}

}  // namespace fwupd_utils
}  // namespace diagnostics
