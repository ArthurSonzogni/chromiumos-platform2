// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_ROUTINE_PARAMETER_FETCHER_CONSTANTS_H_
#define DIAGNOSTICS_CROS_HEALTHD_ROUTINE_PARAMETER_FETCHER_CONSTANTS_H_

namespace diagnostics {

// Path to each routine's properties in cros_config.
inline constexpr auto kBatteryCapacityPropertiesPath =
    "/cros-healthd/routines/battery-capacity";
inline constexpr auto kBatteryHealthPropertiesPath =
    "/cros-healthd/routines/battery-health";
inline constexpr auto kPrimeSearchPropertiesPath =
    "/cros-healthd/routines/prime-search";

// Battery capacity properties read from cros_config.
inline constexpr auto kLowMahProperty = "low-mah";
inline constexpr auto kHighMahProperty = "high-mah";

// Battery health properties read from cros_config.
inline constexpr auto kMaximumCycleCountProperty = "maximum-cycle-count";
inline constexpr auto kPercentBatteryWearAllowedProperty =
    "percent-battery-wear-allowed";

// Prime search property read from cros_config.
inline constexpr auto kMaxNumProperty = "max-num";

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_ROUTINE_PARAMETER_FETCHER_CONSTANTS_H_
