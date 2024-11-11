// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libec/fourcc.h"

#include <array>
#include <cctype>
#include <format>
#include <string>

namespace ec {

std::string FourCCToString(uint32_t a) {
  const std::array<char, 4> chars = {
      static_cast<char>(a),
      static_cast<char>(a >> 8),
      static_cast<char>(a >> 16),
      static_cast<char>(a >> 24),
  };

  // The unsigned char conversion is intentional. See isprint documentation.
  for (unsigned char c : chars) {
    if (!std::isprint(c)) {
      return std::format("0x{:X}", a);
    }
  }

  return std::string(chars.data(), chars.size());
}

}  // namespace ec
