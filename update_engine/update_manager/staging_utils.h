// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_UPDATE_MANAGER_STAGING_UTILS_H_
#define UPDATE_ENGINE_UPDATE_MANAGER_STAGING_UTILS_H_

#include <utility>
#include <vector>

#include <base/time/time.h>
#include <policy/device_policy.h>

#include "update_engine/common/prefs_interface.h"

namespace chromeos_update_manager {

using StagingSchedule = std::vector<policy::DevicePolicy::DayPercentagePair>;

// Possible cases that staging might run into based on the inputs.
enum class StagingCase {
  // Staging is off, remove the persisted value.
  kOff,
  // Staging is enabled, but there is no valid persisted value, saved value or
  // the value of the schedule has changed.
  kNoSavedValue,
  // Staging is enabled, and there is a valid persisted value.
  kSetStagingFromPref,
  // Staging is enabled, and there have been no changes to the schedule.
  kNoAction
};

// Calculate the bucket in which the device belongs based on a given staging
// schedule. |staging_schedule| is assumed to have already been validated.
base::TimeDelta CalculateWaitTimeFromSchedule(
    const StagingSchedule& staging_schedule);

// Verifies that |device_policy| contains a valid staging schedule. If
// |device_policy| contains a valid staging schedule, move it into
// |staging_schedule_out| and return the total number of days spanned by the
// schedule. Otherwise, don't modify |staging_schedule_out| and return 0 (which
// is an invalid value for the length of a schedule).
int GetStagingSchedule(const policy::DevicePolicy* device_policy,
                       StagingSchedule* staging_schedule_out);

// Uses the given arguments to check whether staging is on, and whether the
// state should be updated with a new waiting time or not. |staging_wait_time|
// should contain the old value of the wait time, it will be replaced with the
// new calculated wait time value if staging is on. |staging_schedule| should
// contain the previous staging schedule, if there is a new schedule found, its
// value will be replaced with the new one.
StagingCase CalculateStagingCase(const policy::DevicePolicy* device_policy,
                                 base::TimeDelta* staging_wait_time,
                                 StagingSchedule* staging_schedule);

}  // namespace chromeos_update_manager

#endif  // UPDATE_ENGINE_UPDATE_MANAGER_STAGING_UTILS_H_
