// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_SYSTEM_GROUND_TRUTH_CONSTANTS_H_
#define DIAGNOSTICS_CROS_HEALTHD_SYSTEM_GROUND_TRUTH_CONSTANTS_H_

namespace diagnostics {

namespace cros_config_value {

// Possible values of /hardware-properties/form-factor.
inline constexpr char kClamshell[] = "CLAMSHELL";
inline constexpr char kConvertible[] = "CONVERTIBLE";
inline constexpr char kDetachable[] = "DETACHABLE";
inline constexpr char kChromebase[] = "CHROMEBASE";
inline constexpr char kChromebox[] = "CHROMEBOX";
inline constexpr char kChromebit[] = "CHROMEBIT";
inline constexpr char kChromeslate[] = "CHROMESLATE";

// Possible values of /hardware-properties/stylus-category.
inline constexpr char kStylusCategoryUnknown[] = "unknown";
inline constexpr char kStylusCategoryNone[] = "none";
inline constexpr char kStylusCategoryInternal[] = "internal";
inline constexpr char kStylusCategoryExternal[] = "external";

// Possible values of /hardware-properties/storage-type.
// Other unused possible values: NVME, SATA, BRIDGED_EMMC, UFS.
inline constexpr char kStorageTypeUnknown[] = "STORAGE_TYPE_UNKNOWN";
inline constexpr char kStorageTypeEmmc[] = "EMMC";

}  // namespace cros_config_value

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_SYSTEM_GROUND_TRUTH_CONSTANTS_H_
