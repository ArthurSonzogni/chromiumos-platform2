// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_SYSTEM_GROUND_TRUTH_CONSTANTS_H_
#define DIAGNOSTICS_CROS_HEALTHD_SYSTEM_GROUND_TRUTH_CONSTANTS_H_

namespace diagnostics {

namespace cros_config_path {

inline constexpr char kHardwareProperties[] = "/hardware-properties";

}  // namespace cros_config_path

namespace cros_config_property {

inline constexpr char kFormFactor[] = "form-factor";

}  // namespace cros_config_property

namespace cros_config_value {

// Possible values of /hardware-properties/form-factor.
inline constexpr char kClamshell[] = "CLAMSHELL";
inline constexpr char kConvertible[] = "CONVERTIBLE";
inline constexpr char kDetachable[] = "DETACHABLE";
inline constexpr char kChromebase[] = "CHROMEBASE";
inline constexpr char kChromebox[] = "CHROMEBOX";
inline constexpr char kChromebit[] = "CHROMEBIT";
inline constexpr char kChromeslate[] = "CHROMESLATE";

}  // namespace cros_config_value

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_SYSTEM_GROUND_TRUTH_CONSTANTS_H_
