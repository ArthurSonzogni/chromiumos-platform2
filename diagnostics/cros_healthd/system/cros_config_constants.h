// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_SYSTEM_CROS_CONFIG_CONSTANTS_H_
#define DIAGNOSTICS_CROS_HEALTHD_SYSTEM_CROS_CONFIG_CONSTANTS_H_

namespace diagnostics {

namespace cros_config_value {

inline constexpr char kTrue[] = "true";

// Possible values of /hardware-properties/storage-type.
// Other unused possible values: STORAGE_TYPE_UNKNOWN, EMMC, NVME, SATA,
// BRIDGED_EMMC.
inline constexpr char kStorageTypeUfs[] = "UFS";

}  // namespace cros_config_value

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_SYSTEM_CROS_CONFIG_CONSTANTS_H_
