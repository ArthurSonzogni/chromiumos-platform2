// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/proto/priority_name.h"

#include <string>

#include "missive/proto/record_constants.pb.h"

namespace reporting {

// Temporary replacement for `Priority_Name` that does
// not work in certain CQ.
// TODO(b/294756107): Remove this function once fixed.
std::string Priority_Name_Substitute(int priority) {
  static const std::array<std::string, Priority_ARRAYSIZE> names = {
      "UNDEFINED_PRIORITY",   // 0
      "IMMEDIATE",            // 1
      "FAST_BATCH",           // 2
      "SLOW_BATCH",           // 3
      "BACKGROUND_BATCH",     // 4
      "MANUAL_BATCH",         // 5
      "SECURITY",             // 6
      "MANUAL_BATCH_LACROS",  // 7
  };
  if (!Priority_IsValid(priority)) {
    return "";
  }
  return names[priority];
}
}  // namespace reporting
