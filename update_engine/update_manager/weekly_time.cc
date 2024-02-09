// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/update_manager/weekly_time.h"

#include <base/strings/string_number_conversions.h>
#include <base/strings/stringprintf.h>
#include <base/time/time.h>

using base::Time;
using base::TimeDelta;
using std::string;

namespace {
const TimeDelta kTimeInWeek = base::Days(7);
}

namespace chromeos_update_manager {

TimeDelta WeeklyTime::GetDurationTo(const WeeklyTime& other) const {
  if (other.TimeFromStartOfWeek() < TimeFromStartOfWeek()) {
    return other.TimeFromStartOfWeek() + (kTimeInWeek - TimeFromStartOfWeek());
  }
  return other.TimeFromStartOfWeek() - TimeFromStartOfWeek();
}

TimeDelta WeeklyTime::TimeFromStartOfWeek() const {
  return base::Days(day_of_week_) + time_;
}

void WeeklyTime::AddTime(const TimeDelta& offset) {
  time_ += offset;
  int days_over = time_.InDays();
  time_ -= base::Days(days_over);
  day_of_week_ = (day_of_week_ + days_over - 1) % kTimeInWeek.InDays() + 1;
}

// static
WeeklyTime WeeklyTime::FromTime(const Time& time) {
  Time::Exploded exploded;
  time.LocalExplode(&exploded);
  return WeeklyTime(exploded.day_of_week, base::Hours(exploded.hour) +
                                              base::Minutes(exploded.minute));
}

bool WeeklyTimeInterval::InRange(const WeeklyTime& time) const {
  return time == start_ ||
         (time.GetDurationTo(start_) >= time.GetDurationTo(end_) &&
          time != end_);
}

string WeeklyTimeInterval::ToString() const {
  return base::StringPrintf(
      "Start: day_of_week=%d time=%d\nEnd: day_of_week=%d time=%d",
      start_.day_of_week(), start_.time().InMinutes(), end_.day_of_week(),
      end_.time().InMinutes());
}

}  // namespace chromeos_update_manager
