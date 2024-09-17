// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SWAP_MANAGEMENT_SUSPEND_HISTORY_H_
#define SWAP_MANAGEMENT_SUSPEND_HISTORY_H_

#include <deque>
#include <optional>

#include <base/containers/circular_deque.h>
#include <base/sequence_checker.h>
#include <base/time/time.h>

#include "swap_management/utils.h"

namespace swap_management {

void UpdateBoottimeForTesting(std::optional<base::TimeTicks>);

// SuspendHistory tracks the duration of suspends.
//
// Zram writeback marks idle pages based on boottime clock timestamp which keeps
// ticking even while the device is suspended. This can end up marking
// relatively new pages as idle. For example, when the threshold for idle page
// is 25 hours and the user suspends the device whole the weekend (i.e. 2days),
// all pages in zram are marked as idle which is too aggressive.
//
// ChromeOS mitigates the issue by adjusting the idle threshold by the actual
// duration of the device is suspended in swap_management service because fixing
// the kernel to use monotonic clock instead of boottime clock can break
// existing user space behavior.
//
// The adjustment duration is calculated by CalculateTotalSuspendedDuration().
// For example, if the idle threshold is 4 hours just after these usage log:
//
// * User suspends 1 hours (A) and use the device for 2 hours and,
// * User suspends 5 hours (B) and use the device for 1 hours and,
// * User suspends 2 hours (C) and use the device for 1 hours and,
// * User suspends 1 hours (D) and use the device for 1 hours
//
// In this case, the threshold need to be adjusted by 8 hours (B + C + D).
//
// ```
//                                                      now
// log       : |-A-|     |----B----|   |--C--|   |-D-|   |
// threshold :                            |---original---|
// adjustment:        |----B----|--C--|-D-|
// ```
//
// SuspendHistory uses deque to store the suspend logs. Each entry is 16 bytes.
// At worst case, even if a user repeats suspend and resume every second for 25
// hours, the deque consumes only about 1.5MB. Zram writeback occurs every an
// hour. Traversing 1.5MB every hour is an acceptable cost.
//
// This is not thread safe.
class SuspendHistory {
 public:
  SuspendHistory(const SuspendHistory&) = delete;
  SuspendHistory& operator=(const SuspendHistory&) = delete;

  static SuspendHistory* Get();

  void SetMaxIdleDuration(base::TimeDelta max);

  void OnSuspendImminent();
  void OnSuspendDone(base::TimeDelta suspend_duration);

  // Returns true if the system is logically suspended. Useful to determine when
  // code is executing during dark resume.
  bool IsSuspended();
  base::TimeDelta CalculateTotalSuspendedDuration(
      base::TimeDelta target_idle_duration);

 private:
  SuspendHistory();
  ~SuspendHistory() = default;

  friend class MockSuspendHistory;

  friend SuspendHistory** GetSingleton<SuspendHistory>();

  struct Entry {
    base::TimeTicks wake_up_at_;
    base::TimeDelta suspend_duration_;
  };
  std::deque<Entry> suspend_history_;
  bool is_suspended_ = false;
  base::TimeDelta total_awake_duration_;
  base::TimeDelta max_idle_duration_;

  SEQUENCE_CHECKER(sequence_checker_);
};

}  // namespace swap_management

#endif  // SWAP_MANAGEMENT_SUSPEND_HISTORY_H_
