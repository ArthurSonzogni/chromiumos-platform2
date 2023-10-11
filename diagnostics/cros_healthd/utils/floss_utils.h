// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_UTILS_FLOSS_UTILS_H_
#define DIAGNOSTICS_CROS_HEALTHD_UTILS_FLOSS_UTILS_H_

#include <optional>
#include <string>
#include <vector>

#include <brillo/variant_dictionary.h>
#include <dbus/object_path.h>

namespace diagnostics::floss_utils {

// Parse and convert 128 bits UUID to the string of 32 hexadecimal lowercase
// characters in the 8-4-4-4-12 format.
std::optional<std::string> ParseUuidBytes(const std::vector<uint8_t>& bytes);

}  // namespace diagnostics::floss_utils

#endif  // DIAGNOSTICS_CROS_HEALTHD_UTILS_FLOSS_UTILS_H_
