// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libec/fourcc.h"

#include <string>

#include "base/strings/stringprintf.h"

namespace ec {

std::string FourCCToString(uint32_t a) {
  return base::StringPrintf(
      "%c%c%c%c", static_cast<char>(a), static_cast<char>(a >> 8),
      static_cast<char>(a >> 16), static_cast<char>(a >> 24));
}

}  // namespace ec
