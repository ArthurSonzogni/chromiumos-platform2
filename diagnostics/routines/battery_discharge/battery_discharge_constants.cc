// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/routines/battery_discharge/battery_discharge_constants.h"

namespace diagnostics {

const char kBatteryDischargeRoutineSucceededMessage[] =
    "Battery discharge routine passed.";
const char kBatteryDischargeRoutineNotDischargingMessage[] =
    "Battery is not discharging.";
const char kBatteryDischargeRoutineFailedExcessiveDischargeMessage[] =
    "Battery discharge rate greater than maximum allowed discharge rate.";
const char kBatteryDischargeRoutineFailedReadingBatteryAttributesMessage[] =
    "Failed to read battery attributes from sysfs.";
const char kBatteryDischargeRoutineInvalidParametersMessage[] =
    "Maximum allowed discharge percent must be less than or equal to 100.";
const char kBatteryDischargeRoutineCancelledMessage[] =
    "Battery discharge routine cancelled.";
const char kBatteryDischargeRoutineRunningMessage[] =
    "Battery discharge routine running.";

const char kBatteryDirectoryPath[] = "sys/class/power_supply/BAT0";
const char kBatteryChargeNowFileName[] = "charge_now";
const char kBatteryChargeFullFileName[] = "charge_full";
const char kBatteryStatusFileName[] = "status";

const char kBatteryStatusDischargingValue[] = "Discharging";

}  // namespace diagnostics
