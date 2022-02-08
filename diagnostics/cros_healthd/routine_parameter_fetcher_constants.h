// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_ROUTINE_PARAMETER_FETCHER_CONSTANTS_H_
#define DIAGNOSTICS_CROS_HEALTHD_ROUTINE_PARAMETER_FETCHER_CONSTANTS_H_

namespace diagnostics {

// Path to each routine's properties in cros_config.
inline constexpr char kBatteryCapacityPropertiesPath[] =
    "/cros-healthd/routines/battery-capacity";
inline constexpr char kBatteryHealthPropertiesPath[] =
    "/cros-healthd/routines/battery-health";
inline constexpr char kPrimeSearchPropertiesPath[] =
    "/cros-healthd/routines/prime-search";

// Battery capacity properties read from cros_config.
inline constexpr char kLowMahProperty[] = "low-mah";
inline constexpr char kHighMahProperty[] = "high-mah";

// Battery health properties read from cros_config.
inline constexpr char kMaximumCycleCountProperty[] = "maximum-cycle-count";
inline constexpr char kPercentBatteryWearAllowedProperty[] =
    "percent-battery-wear-allowed";

// Prime search property read from cros_config.
inline constexpr char kMaxNumProperty[] = "max-num";

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_ROUTINE_PARAMETER_FETCHER_CONSTANTS_H_
