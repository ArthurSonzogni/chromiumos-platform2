// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_COMMON_TYPES_H_
#define RMAD_COMMON_TYPES_H_

#include <string>

namespace rmad {

// Keep this in sync with metrics/structured/structured.xml.
enum class WpDisableMethod : int {
  UNKNOWN = 0,
  SKIPPED = 1,
  RSU = 2,
  PHYSICAL_ASSEMBLE_DEVICE = 3,
  PHYSICAL_KEEP_DEVICE_OPEN = 4,
};

std::string WpDisableMethod_Name(WpDisableMethod method);
bool WpDisableMethod_Parse(const std::string& name, WpDisableMethod* method);

}  // namespace rmad

#endif  // RMAD_COMMON_TYPES_H_
