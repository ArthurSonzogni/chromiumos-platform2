// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NETWORK_TRIAL_SCHEDULER_H_
#define SHILL_NETWORK_TRIAL_SCHEDULER_H_

#include <base/functional/callback_forward.h>
#include <base/memory/weak_ptr.h>
#include <base/time/time.h>

#include "shill/event_dispatcher.h"

namespace shill {

// This class performs the exponential backoff scheduling strategy:
// - The 1st trial is triggered immediately when ScheduleTrial() is called.
// - The interval between 1st and 2nd trials being triggered is greater or equal
//   to kBaseInterval.
// - The interval between the following trials being triggered grows
//   exponentially (i.e. the interval will be doubled each time) until saturated
//   to kMaxInterval.
class TrialScheduler {
 public:
  // Base time interval between two trials. Should be doubled at every new
  // trial.
  static constexpr base::TimeDelta kBaseInterval = base::Seconds(3);
  // Min time delay between two trials.
  static constexpr base::TimeDelta kMinDelay = base::Seconds(0);
  // Max time interval between two trials.
  static constexpr base::TimeDelta kMaxInterval = base::Minutes(1);

  explicit TrialScheduler(EventDispatcher* dispatcher);
  ~TrialScheduler();

  // Schedules a new trial with the exponential backoff strategy. Returns false
  // and does nothing if there is already a pending trial scheduled.
  bool ScheduleTrial(base::OnceClosure trial);

  // Cancels the scheduled trial if exists. The interval between the last trial
  // and the next trial won't be affected.
  void CancelTrial();

  // Returns true if a trial is scheduled but hasn't been executed.
  bool IsTrialScheduled() const;

  // Resets the interval to 0. The next scheduled trial will be executed
  // immediately. It doesn't affect the pending trial if it exists.
  void ResetInterval();

 private:
  // Executes the scheduled trial.
  void ExecuteTrial();

  // Updates the interval of the next trial.
  void UpdateNextInterval();

  // Calculates the delay of the next trial from now.
  base::TimeDelta GetNextTrialDelay() const;

  // Used to execute the trial in delay.
  EventDispatcher* dispatcher_;

  // The scheduled trial.
  base::OnceClosure trial_;

  // Timestamp updated when ExecuteTrial() runs.
  base::TimeTicks last_trial_start_time_;

  // The interval between the last trial and the next trial.
  base::TimeDelta next_interval_;

  base::WeakPtrFactory<TrialScheduler> weak_ptr_factory_{this};
};

}  // namespace shill
#endif  // SHILL_NETWORK_TRIAL_SCHEDULER_H_
