// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_INTERNAL_BACKLIGHT_H_
#define POWER_MANAGER_POWERD_SYSTEM_INTERNAL_BACKLIGHT_H_

#include <stdint.h>

#include <base/files/file_path.h>
#include <base/macros.h>
#include <base/memory/scoped_ptr.h>
#include <base/time/time.h>
#include <base/timer/timer.h>

#include "power_manager/powerd/system/backlight_interface.h"

namespace power_manager {

class Clock;

namespace system {

// Controls a panel or keyboard backlight via sysfs.
class InternalBacklight : public BacklightInterface {
 public:
  // Base names of backlight files within sysfs directories.
  static const char kBrightnessFilename[];
  static const char kMaxBrightnessFilename[];
  static const char kActualBrightnessFilename[];
  static const char kResumeBrightnessFilename[];
  static const char kBlPowerFilename[];

  InternalBacklight();
  virtual ~InternalBacklight();

  // Initialize the backlight object.
  //
  // The |base_path| specifies the directory to look for backlights.  The
  // |pattern| is a glob pattern to help find the right backlight.
  // Expected values for parameters look like:
  //   base: "/sys/class/backlight", pattern: "*"
  //   base: "/sys/class/leds", pattern: "*:kbd_backlight"
  //
  // On success, return true; otherwise return false.
  bool Init(const base::FilePath& base_path,
            const base::FilePath::StringType& pattern);

  bool transition_timer_is_running() const {
    return transition_timer_.IsRunning();
  }
  base::TimeTicks transition_timer_start_time() const {
    return transition_timer_start_time_;
  }
  Clock* clock() { return clock_.get(); }

  // Calls HandleTransitionTimeout() as if |transition_timer_| had fired
  // and returns true if the timer is still running afterward and false if it
  // isn't.
  bool TriggerTransitionTimeoutForTesting();

  // Overridden from BacklightInterface:
  int64_t GetMaxBrightnessLevel() override;
  int64_t GetCurrentBrightnessLevel() override;
  bool SetBrightnessLevel(int64_t level, base::TimeDelta interval) override;
  bool SetResumeBrightnessLevel(int64_t level) override;
  bool TransitionInProgress() const override;

 private:
  // Helper method that actually writes to |brightness_path_| and updates
  // |current_brightness_level_|, also writing to |bl_power_path_| if necessary.
  // Called by SetBrightnessLevel() and HandleTransitionTimeout(). Returns true
  // on success.
  bool WriteBrightness(int64_t new_level);

  // Sets the brightness level appropriately for the current point in the
  // transition.  When the transition is done, stops |transition_timer_|.
  void HandleTransitionTimeout();

  // Cancels |transition_timeout_id_| if set.
  void CancelTransition();

  scoped_ptr<Clock> clock_;

  // Paths to the actual_brightness, brightness, max_brightness and
  // resume_brightness files under /sys/class/backlight.
  base::FilePath actual_brightness_path_;
  base::FilePath brightness_path_;
  base::FilePath max_brightness_path_;
  base::FilePath resume_brightness_path_;

  // Path to a bl_power file in sysfs that can be used to turn the backlight on
  // or off. Empty if the file isn't present. See http://crbug.com/396218 for
  // details.
  base::FilePath bl_power_path_;

  // Cached maximum and last-set brightness levels.
  int64_t max_brightness_level_;
  int64_t current_brightness_level_;

  // Calls HandleTransitionTimeout().
  base::RepeatingTimer<InternalBacklight> transition_timer_;

  // Time at which |transition_timer_| was last started. Used for testing.
  base::TimeTicks transition_timer_start_time_;

  // Times at which the current transition started and is scheduled to end.
  base::TimeTicks transition_start_time_;
  base::TimeTicks transition_end_time_;

  // Start and end brightness level for the current transition.
  int64_t transition_start_level_;
  int64_t transition_end_level_;

  DISALLOW_COPY_AND_ASSIGN(InternalBacklight);
};

}  // namespace system
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_SYSTEM_INTERNAL_BACKLIGHT_H_
