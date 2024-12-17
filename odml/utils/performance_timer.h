// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_UTILS_PERFORMANCE_TIMER_H_
#define ODML_UTILS_PERFORMANCE_TIMER_H_

#include <memory>

#include <base/time/time.h>

namespace odml {

class PerformanceTimer {
 public:
  using Ptr = std::unique_ptr<PerformanceTimer>;

  PerformanceTimer();
  PerformanceTimer(const PerformanceTimer&) = delete;

  static Ptr Create();

  // Get the duration that has passed since `start_time_`.
  base::TimeDelta GetDuration() const;

 private:
  base::TimeTicks start_time_;
};

}  // namespace odml

#endif  // ODML_UTILS_PERFORMANCE_TIMER_H_
