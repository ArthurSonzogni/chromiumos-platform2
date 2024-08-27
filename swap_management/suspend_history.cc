// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "swap_management/suspend_history.h"

#include <time.h>

#include <optional>

#include <base/logging.h>
#include <base/time/time.h>

namespace swap_management {

namespace {

std::optional<base::TimeTicks> g_current_boottime_for_testing_ = std::nullopt;

// TODO(kawasin): Copied from Chrome's //base/time/time_now_posix.cc. Make
// upstream code available via libchrome and use it here:
// http://crbug.com/166153.
int64_t ConvertTimespecToMicros(const struct timespec& ts) {
  // On 32-bit systems, the calculation cannot overflow int64_t.
  // 2**32 * 1000000 + 2**64 / 1000 < 2**63
  if (sizeof(ts.tv_sec) <= 4 && sizeof(ts.tv_nsec) <= 8) {
    int64_t result = ts.tv_sec;
    result *= base::Time::kMicrosecondsPerSecond;
    result += (ts.tv_nsec / base::Time::kNanosecondsPerMicrosecond);
    return result;
  } else {
    base::CheckedNumeric<int64_t> result(ts.tv_sec);
    result *= base::Time::kMicrosecondsPerSecond;
    result += (ts.tv_nsec / base::Time::kNanosecondsPerMicrosecond);
    return result.ValueOrDie();
  }
}

// TODO(kawasin): Copied from Chrome's //base/time/time_now_posix.cc. Make
// upstream code available via libchrome and use it here:
// http://crbug.com/166153.
// Returns count of |clk_id|. Returns 0 if |clk_id| isn't present on the system.
int64_t ClockNow(clockid_t clk_id) {
  struct timespec ts;
  if (clock_gettime(clk_id, &ts) != 0) {
    return 0;
  }
  return ConvertTimespecToMicros(ts);
}

base::TimeTicks GetCurrentBootTime() {
  if (g_current_boottime_for_testing_.has_value()) {
    return *g_current_boottime_for_testing_;
  }
  return base::TimeTicks() + base::Microseconds(ClockNow(CLOCK_BOOTTIME));
}

}  // namespace

void UpdateBoottimeForTesting(std::optional<base::TimeTicks> value) {
  g_current_boottime_for_testing_ = value;
}

SuspendHistory::SuspendHistory() {
  base::TimeTicks now = GetCurrentBootTime();
  suspend_history_.push_front(Entry{
      now,
      base::TimeDelta(),
  });
}

void SuspendHistory::SetMaxIdleDuration(base::TimeDelta max) {
  max_idle_duration_ = max;
}

void SuspendHistory::OnSuspendImminent() {
  is_suspended_ = true;
}

void SuspendHistory::OnSuspendDone(base::TimeDelta suspend_duration) {
  base::TimeTicks now = GetCurrentBootTime();
  base::TimeDelta awake_duration =
      now - suspend_duration - suspend_history_.front().wake_up_at_;
  total_awake_duration_ += awake_duration;

  while (total_awake_duration_ > max_idle_duration_ &&
         suspend_history_.size() >= 2) {
    base::TimeTicks oldest_wake_at = suspend_history_.back().wake_up_at_;
    suspend_history_.pop_back();
    base::TimeDelta oldest_awake_duration =
        suspend_history_.back().wake_up_at_ -
        suspend_history_.back().suspend_duration_ - oldest_wake_at;
    total_awake_duration_ -= oldest_awake_duration;
  }

  suspend_history_.push_front(Entry{
      now,
      suspend_duration,
  });

  is_suspended_ = false;
}

bool SuspendHistory::IsSuspended() {
  return is_suspended_;
}

base::TimeDelta SuspendHistory::CalculateTotalSuspendedDuration(
    base::TimeDelta target_idle_duration) {
  base::TimeTicks now = GetCurrentBootTime();
  base::TimeTicks target_time = now - target_idle_duration;
  base::TimeDelta total_suspended_duration = base::TimeDelta();
  for (const auto& entry : suspend_history_) {
    if (entry.wake_up_at_ > (target_time - total_suspended_duration)) {
      total_suspended_duration += entry.suspend_duration_;
    } else {
      break;
    }
  }
  return total_suspended_duration;
}

}  // namespace swap_management
