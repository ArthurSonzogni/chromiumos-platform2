// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_CORAL_DELAYED_REPEATING_TIMER_H_
#define ODML_CORAL_DELAYED_REPEATING_TIMER_H_

#include <base/memory/scoped_refptr.h>
#include <base/task/sequenced_task_runner.h>
#include <base/time/time.h>
#include <base/timer/timer.h>

namespace coral {

// DelayedRepeatingTimer is a timer that introduces a specified delay
// (start_delay) before the first run of user_task, then repeatedly runs
// user_task every repeat_delay.
class DelayedRepeatingTimer {
 public:
  // start_delay is the initial delay before the first execution of the user
  // task. repeat_delay is the delay between subsequent executions of the user
  // task. user_task is the callback to be executed after the delays.
  DelayedRepeatingTimer(base::TimeDelta start_delay,
                        base::TimeDelta repeat_delay,
                        base::RepeatingCallback<void()> user_task);

  // Starts the timer. This cancels any existing timers and starts the timer.
  void Start();

  // Stops the timer. This prevents further executions of the user task.
  void Stop();

 private:
  // This is a help that runs after the starting delay and starts the repeating
  // timer.
  void OnStartingDelayTimesUp();

  // The initial delay before the first execution.
  base::TimeDelta start_delay_;
  // The delay between subsequent executions.
  base::TimeDelta repeat_delay_;

  // Called every time the timer times up.
  base::RepeatingCallback<void()> user_task_;

  // Timer for the initial delay, fires only once per Start().
  base::OneShotTimer one_shot_timer_;
  // Timer for the repeating execution, fires multiple times per Start().
  base::RepeatingTimer repeating_timer_;
};

}  // namespace coral

#endif  // ODML_CORAL_DELAYED_REPEATING_TIMER_H_
