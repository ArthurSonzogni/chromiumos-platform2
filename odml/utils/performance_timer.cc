// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/utils/performance_timer.h"

#include <memory>

#include <base/time/time.h>

namespace odml {

PerformanceTimer::PerformanceTimer() : start_time_(base::TimeTicks::Now()) {}

PerformanceTimer::Ptr PerformanceTimer::Create() {
  return std::make_unique<PerformanceTimer>();
}

base::TimeDelta PerformanceTimer::GetDuration() const {
  return base::TimeTicks::Now() - start_time_;
}

}  // namespace odml
