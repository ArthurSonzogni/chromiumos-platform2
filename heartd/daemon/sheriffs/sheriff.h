// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HEARTD_DAEMON_SHERIFFS_SHERIFF_H_
#define HEARTD_DAEMON_SHERIFFS_SHERIFF_H_

#include <base/timer/timer.h>

namespace heartd {

class Sheriff {
 public:
  virtual ~Sheriff() = default;

  // This is called by TopSheriff to ask the sheriff start working.
  virtual void GetToWork() {
    // One shot work.
    if (!is_one_shot_work_called_) {
      OneShotWork();
      is_one_shot_work_called_ = true;
    }

    // Shift work.
    if (timer_.IsRunning() || !HasShiftWork()) {
      return;
    }

    AdjustSchedule();
    timer_.Start(
        FROM_HERE, schedule_,
        base::BindRepeating(&Sheriff::ShiftWork, base::Unretained(this)));
  }

  // One shot work. This will be called before starting the shift.
  virtual void OneShotWork() {}

  // This is called by Sheriff::StartShift to determine if this sheriff has
  // shift work.
  virtual bool HasShiftWork() = 0;

  // Returns if the sheriff is working.
  virtual bool IsWorking() { return timer_.IsRunning(); }

  // This is called by Sheriff::GetToWork to adjust the schedule.
  virtual void AdjustSchedule() {}

  // Sheriff's shift work.
  virtual void ShiftWork() {}

  // This is called by TopSheriff to clean up.
  virtual void CleanUp() {}

 protected:
  // The timer to run the ShiftWork().
  base::RepeatingTimer timer_;
  // The schedule to run the ShiftWork().
  base::TimeDelta schedule_ = base::Hours(1);

 private:
  // Uses to prevent duplicate call of OneShotWork().
  bool is_one_shot_work_called_ = false;
};

}  // namespace heartd

#endif  // HEARTD_DAEMON_SHERIFFS_SHERIFF_H_
