// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_UPDATE_MANAGER_UPDATE_TIME_RESTRICTIONS_MONITOR_H_
#define UPDATE_ENGINE_UPDATE_MANAGER_UPDATE_TIME_RESTRICTIONS_MONITOR_H_

#include <memory>

#include <base/memory/weak_ptr.h>
#include <brillo/message_loops/message_loop.h>

#include "update_engine/update_manager/device_policy_provider.h"
#include "update_engine/update_manager/evaluation_context.h"
#include "update_engine/update_manager/weekly_time.h"

namespace chromeos_update_manager {

// Represents a monitor tracking start of restricted time intervals during which
// update download is not allowed. It reads |var_disallowed_time_intervals|,
// chooses the next interval according to current time, awaits its start and
// notifies the delegate. If the chosen interval is already happening, the
// monitor notifies immediately. The monitor will never notify the delegate
// while the current list of restricted intervals is empty.
//
// The monitor detects changes in the restricted intervals and handles the
// change with following cases:
// 1. No restricted time intervals or none of the intervals is in progress -> no
//    new restricted intervals or none of the new intervals matches the current
//    time.
//    The monitor starts tracking the next interval from the new ones, if any.
// 2. No restricted time intervals or none of the intervals is in progress ->
//    there is a new interval matching current time.
//    The monitor shall pick this new interval and notify the delegate
//    immediately about the start of the restricted interval.
class UpdateTimeRestrictionsMonitor {
 public:
  // Interface to handle start of a restricted time interval.
  class Delegate {
   public:
    virtual ~Delegate() = default;

    virtual void OnRestrictedIntervalStarts() = 0;
  };

  // Creates an instance and starts monitoring the next nearest restricted time
  // interval if present. If no intervals are available yet the monitor will be
  // idle until intervals list changes.
  UpdateTimeRestrictionsMonitor(DevicePolicyProvider* device_policy_provider,
                                Delegate* delegate);

  UpdateTimeRestrictionsMonitor(const UpdateTimeRestrictionsMonitor&) = delete;
  UpdateTimeRestrictionsMonitor& operator=(
      const UpdateTimeRestrictionsMonitor&) = delete;

  ~UpdateTimeRestrictionsMonitor();

  bool IsMonitoringInterval() {
    return timeout_event_ != brillo::MessageLoop::kTaskIdNull;
  }

 private:
  // Starts monitoring the start of nearest restricted time interval if present
  // and any change in restricted time intervals from policy.
  void StartMonitoring();
  void WaitForRestrictedIntervalStarts(
      const WeeklyTimeIntervalVector& restricted_time_intervals);

  // Called when current time lies within a restricted interval.
  void HandleRestrictedIntervalStarts();

  // Stop monotoring any restricted intervals.
  void StopMonitoring();

  // Called upon change of restricted intervals.
  void OnIntervalsChanged();

  // To access restricted time intervals from |device_policy_provider_|.
  EvaluationContext evaluation_context_;

  DevicePolicyProvider* const device_policy_provider_;
  Delegate* const delegate_;

  // The TaskId returned by the message loop identifying the timeout callback.
  // Used for cancelling the timeout callback.
  brillo::MessageLoop::TaskId timeout_event_{brillo::MessageLoop::kTaskIdNull};

  base::WeakPtrFactory<UpdateTimeRestrictionsMonitor> weak_ptr_factory_;
};

}  // namespace chromeos_update_manager

#endif  // UPDATE_ENGINE_UPDATE_MANAGER_UPDATE_TIME_RESTRICTIONS_MONITOR_H_
