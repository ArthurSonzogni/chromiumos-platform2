// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_ROUTINES_BATTERY_CHARGE_BATTERY_CHARGE_CONSTANTS_H_
#define DIAGNOSTICS_CROS_HEALTHD_ROUTINES_BATTERY_CHARGE_BATTERY_CHARGE_CONSTANTS_H_

namespace diagnostics {

// Status messages reported by the battery charge routine.
inline constexpr auto kBatteryChargeRoutineSucceededMessage =
    "Battery charge routine passed.";
inline constexpr auto kBatteryChargeRoutineNotChargingMessage =
    "Battery is not charging.";
inline constexpr auto kBatteryChargeRoutineFailedInsufficientChargeMessage =
    "Battery charge percent less than minimum required charge percent.";
inline constexpr auto
    kBatteryChargeRoutineFailedReadingBatteryAttributesMessage =
        "Failed to read battery attributes from sysfs.";
inline constexpr auto kBatteryChargeRoutineInvalidParametersMessage =
    "Invalid minimum required charge percent requested.";
inline constexpr auto kBatteryChargeRoutineCancelledMessage =
    "Battery charge routine cancelled.";
inline constexpr auto kBatteryChargeRoutineRunningMessage =
    "Battery charge routine running.";

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_ROUTINES_BATTERY_CHARGE_BATTERY_CHARGE_CONSTANTS_H_
