// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_SYSTEM_SYSTEM_CONFIG_CONSTANTS_H_
#define DIAGNOSTICS_CROS_HEALTHD_SYSTEM_SYSTEM_CONFIG_CONSTANTS_H_

namespace diagnostics {

// NVME utility program path.
inline constexpr char kNvmeToolPath[] = "/usr/sbin/nvme";
// Linux device path.
inline constexpr char kDevicePath[] = "/dev";
// Smartctl utility program path.
inline constexpr char kSmartctlToolPath[] = "/usr/sbin/smartctl";

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_SYSTEM_SYSTEM_CONFIG_CONSTANTS_H_
