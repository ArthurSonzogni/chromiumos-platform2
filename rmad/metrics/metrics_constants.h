// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_METRICS_METRICS_CONSTANTS_H_
#define RMAD_METRICS_METRICS_CONSTANTS_H_

#include <array>

#include "rmad/proto_bindings/rmad.pb.h"

namespace rmad {

// JsonStore additional keys for metrics usage.
inline constexpr char kMetrics[] = "metrics";

inline constexpr char kFirstSetupTimestamp[] = "first_setup_timestamp";
inline constexpr char kSetupTimestamp[] = "setup_timestamp";
inline constexpr char kRunningTime[] = "running_time";
inline constexpr char kRoFirmwareVerified[] = "ro_firmware_verified";
inline constexpr char kOccurredErrors[] = "occurred_errors";
inline constexpr char kAdditionalActivities[] = "additional_activities";

// This is a dict of dicts for states store info by |state_case|.
inline constexpr char kStateMetrics[] = "state_metrics";

// The part should be under kStateMetrics[state_case].
// Only used when converting from StateMetricsData to base::Value.
inline constexpr char kStateCase[] = "state_case";
inline constexpr char kStateIsAborted[] = "state_is_aborted";
inline constexpr char kStateSetupTimestamp[] = "state_setup_timestamp";
inline constexpr char kStateOverallTime[] = "state_overall_time";
inline constexpr char kStateTransitionsCount[] = "state_transition_count";
inline constexpr char kStateGetLogCount[] = "state_get_log_count";
inline constexpr char kStateSaveLogCount[] = "state_save_log_count";

constexpr std::array<AdditionalActivity, 3> kExpectedPowerCycleActivities = {
    RMAD_ADDITIONAL_ACTIVITY_SHUTDOWN, RMAD_ADDITIONAL_ACTIVITY_REBOOT,
    RMAD_ADDITIONAL_ACTIVITY_BATTERY_CUTOFF};

constexpr std::array<RmadErrorCode, 6> kExpectedErrorCodes = {
    RMAD_ERROR_NOT_SET,
    RMAD_ERROR_OK,
    RMAD_ERROR_WAIT,
    RMAD_ERROR_EXPECT_REBOOT,
    RMAD_ERROR_EXPECT_SHUTDOWN,
    RMAD_ERROR_RMA_NOT_REQUIRED};

}  // namespace rmad

#endif  // RMAD_METRICS_METRICS_CONSTANTS_H_
