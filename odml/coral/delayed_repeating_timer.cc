// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/coral/delayed_repeating_timer.h"

namespace coral {

DelayedRepeatingTimer::DelayedRepeatingTimer(
    base::TimeDelta start_delay,
    base::TimeDelta repeat_delay,
    base::RepeatingCallback<void()> user_task)
    : start_delay_(start_delay),
      repeat_delay_(repeat_delay),
      user_task_(user_task),
      one_shot_timer_(),
      repeating_timer_() {}

void DelayedRepeatingTimer::Start() {
  one_shot_timer_.Stop();
  repeating_timer_.Stop();

  one_shot_timer_.Start(
      FROM_HERE, start_delay_,
      base::BindOnce(&DelayedRepeatingTimer::OnStartingDelayTimesUp,
                     base::Unretained(this)));
}

void DelayedRepeatingTimer::Stop() {
  one_shot_timer_.Stop();
  repeating_timer_.Stop();
}

void DelayedRepeatingTimer::OnStartingDelayTimesUp() {
  user_task_.Run();
  repeating_timer_.Start(FROM_HERE, repeat_delay_, user_task_);
}

}  // namespace coral
