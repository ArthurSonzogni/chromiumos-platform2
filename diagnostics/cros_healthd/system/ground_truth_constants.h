// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_SYSTEM_GROUND_TRUTH_CONSTANTS_H_
#define DIAGNOSTICS_CROS_HEALTHD_SYSTEM_GROUND_TRUTH_CONSTANTS_H_

namespace diagnostics {

// Used to determine whether a device has a Google EC.
constexpr char kCrosEcSysPath[] = "/sys/class/chromeos/cros_ec";

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

}  // namespace cros_config_value

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_SYSTEM_GROUND_TRUTH_CONSTANTS_H_
