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

namespace diagnostics {

namespace {

// Return the hexadecimal characters of `bytes` in index [`start`, `end`).
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

std::optional<std::string> ParseUuidBytes(const std::vector<uint8_t>& bytes) {
  // UUID should be 128 bits.
  if (bytes.size() != 16) {
    LOG(ERROR) << "Get invalid UUID bytes, size: " << bytes.size();
    return std::nullopt;
  }
  // Convert to 8-4-4-4-12 format: XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX.
  return base::JoinString({BytesToHex(bytes, 0, 4), BytesToHex(bytes, 4, 6),
                           BytesToHex(bytes, 6, 8), BytesToHex(bytes, 8, 10),
                           BytesToHex(bytes, 10, 16)},
                          "-");
}

}  // namespace floss_utils

}  // namespace diagnostics
