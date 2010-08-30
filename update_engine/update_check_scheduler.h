// Copyright (c) 2010 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_PLATFORM_UPDATE_ENGINE_UPDATE_CHECK_SCHEDULER_H__
#define CHROMEOS_PLATFORM_UPDATE_ENGINE_UPDATE_CHECK_SCHEDULER_H__

#include <base/basictypes.h>
#include <glib.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#include "update_engine/update_attempter.h"

namespace chromeos_update_engine {

// UpdateCheckScheduler manages the periodic background update checks. This is
// the basic update check cycle:
//
//    Run
//     |
//     v
// /->ScheduleCheck
// |   |
// |   v
// |  StaticCheck (invoked through a GLib timeout source)
// |   |
// |   v
// |  UpdateAttempter::Update
// |   |
// |   v
// |  SetUpdateStatus (invoked by UpdateAttempter on state transitions)
// |   |
// |   v
// |  ScheduleNextCheck (invoked when UpdateAttempter becomes idle)
// \---/
class UpdateCheckScheduler {
 public:
  static const int kTimeoutOnce;
  static const int kTimeoutPeriodic;
  static const int kTimeoutRegularFuzz;
  static const int kTimeoutMaxBackoff;

  UpdateCheckScheduler(UpdateAttempter* update_attempter);
  virtual ~UpdateCheckScheduler();

  // Initiates the periodic update checks, if necessary.
  void Run();

  // Sets the new update status. This is invoked by UpdateAttempter.
  void SetUpdateStatus(UpdateStatus status);

 private:
  friend class UpdateCheckSchedulerTest;
  FRIEND_TEST(UpdateCheckSchedulerTest, CanScheduleTest);
  FRIEND_TEST(UpdateCheckSchedulerTest, ComputeNextIntervalAndFuzzBackoffTest);
  FRIEND_TEST(UpdateCheckSchedulerTest, ComputeNextIntervalAndFuzzTest);
  FRIEND_TEST(UpdateCheckSchedulerTest, GTimeoutAddSecondsTest);
  FRIEND_TEST(UpdateCheckSchedulerTest, IsBootDeviceRemovableTest);
  FRIEND_TEST(UpdateCheckSchedulerTest, IsOfficialBuildTest);
  FRIEND_TEST(UpdateCheckSchedulerTest, RunBootDeviceRemovableTest);
  FRIEND_TEST(UpdateCheckSchedulerTest, RunNonOfficialBuildTest);
  FRIEND_TEST(UpdateCheckSchedulerTest, RunTest);
  FRIEND_TEST(UpdateCheckSchedulerTest, ScheduleCheckDisabledTest);
  FRIEND_TEST(UpdateCheckSchedulerTest, ScheduleCheckEnabledTest);
  FRIEND_TEST(UpdateCheckSchedulerTest, ScheduleCheckNegativeIntervalTest);
  FRIEND_TEST(UpdateCheckSchedulerTest, ScheduleNextCheckDisabledTest);
  FRIEND_TEST(UpdateCheckSchedulerTest, ScheduleNextCheckEnabledTest);
  FRIEND_TEST(UpdateCheckSchedulerTest, SetUpdateStatusIdleDisabledTest);
  FRIEND_TEST(UpdateCheckSchedulerTest, SetUpdateStatusIdleEnabledTest);
  FRIEND_TEST(UpdateCheckSchedulerTest, SetUpdateStatusNonIdleTest);
  FRIEND_TEST(UpdateCheckSchedulerTest, StaticCheckTest);

  // Wraps GLib's g_timeout_add_seconds so that it can be mocked in tests.
  virtual guint GTimeoutAddSeconds(guint interval, GSourceFunc function);

  // Wrappers for utils functions so that they can be mocked in tests.
  virtual bool IsBootDeviceRemovable();
  virtual bool IsOfficialBuild();

  // Returns true if an update check can be scheduled. An update check should
  // not be scheduled if periodic update checks are disabled or if one is
  // already scheduled.
  bool CanSchedule() { return enabled_ && !scheduled_; }

  // Schedules the next periodic update check |interval| seconds from now
  // randomized by +/- |fuzz|/2.
  void ScheduleCheck(int interval, int fuzz);

  // GLib timeout source callback. Initiates an update check through the update
  // attempter.
  static gboolean StaticCheck(void* scheduler);

  // Schedules the next update check by setting up a timeout source.
  void ScheduleNextCheck();

  // Computes the timeout interval along with its random fuzz range for the next
  // update check by taking into account the last timeout interval as well as
  // the last update status.
  void ComputeNextIntervalAndFuzz(int* next_interval, int* next_fuzz);

  // The UpdateAttempter to use for update checks.
  UpdateAttempter* update_attempter_;

  // True if automatic update checks should be scheduled, false otherwise.
  bool enabled_;

  // True if there's an update check scheduled already, false otherwise.
  bool scheduled_;

  // The timeout interval (before fuzzing) for the last update check.
  int last_interval_;

  DISALLOW_COPY_AND_ASSIGN(UpdateCheckScheduler);
};

}  // namespace chromeos_update_engine

#endif  // CHROMEOS_PLATFORM_UPDATE_ENGINE_CHECK_SCHEDULER_H__
