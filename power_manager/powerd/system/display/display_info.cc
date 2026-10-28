// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/system/display/display_info.h"

#include <string>
#include <tuple>

namespace power_manager::system {

bool DisplayInfo::operator<(const DisplayInfo& rhs) const {
  return std::tie(drm_path.value(), i2c_path.value(), sys_path.value(),
                  connector_status) <
         std::tie(rhs.drm_path.value(), rhs.i2c_path.value(),
                  rhs.sys_path.value(), rhs.connector_status);
}

bool DisplayInfo::operator==(const DisplayInfo& o) const {
  return !(*this < o || o < *this);
}

bool IsInternalDisplayConnector(const base::FilePath& drm_path) {
  const std::string name = drm_path.BaseName().value();
  return name.find("-eDP-") != std::string::npos ||
         name.find("-LVDS-") != std::string::npos ||
         name.find("-DSI-") != std::string::npos;
}

}  // namespace power_manager::system
