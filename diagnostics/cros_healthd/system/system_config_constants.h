// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_SYSTEM_SYSTEM_CONFIG_CONSTANTS_H_
#define DIAGNOSTICS_CROS_HEALTHD_SYSTEM_SYSTEM_CONFIG_CONSTANTS_H_

#include <list>
#include <string>

namespace diagnostics {

// NVME utility program path.
inline constexpr char kNvmeToolPath[] = "/usr/sbin/nvme";
// Linux device path.
inline constexpr char kDevicePath[] = "/dev";
// Smartctl utility program path.
inline constexpr char kSmartctlToolPath[] = "/usr/sbin/smartctl";
// Mmc utility program path.
inline constexpr char kMmcToolPath[] = "/usr/bin/mmc";
// Chromium EC path.
inline constexpr char kChromiumECPath[] = "/sys/class/chromeos/cros_ec";

// Returns a list of wilco board names.
inline const std::list<std::string> GetWilcoBoardNames() {
  return {"sarien", "drallion"};
}

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_SYSTEM_SYSTEM_CONFIG_CONSTANTS_H_
