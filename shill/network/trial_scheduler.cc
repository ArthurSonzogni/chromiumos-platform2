// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/trial_scheduler.h"

#include <algorithm>
#include <utility>

#include <base/functional/bind.h>
#include <base/functional/callback.h>
#include <base/logging.h>
#include <base/memory/weak_ptr.h>
#include <base/time/time.h>

#include "shill/event_dispatcher.h"

namespace shill {

TrialScheduler::TrialScheduler(EventDispatcher* dispatcher)
    : dispatcher_(dispatcher) {}

TrialScheduler::~TrialScheduler() = default;

bool TrialScheduler::ScheduleTrial(base::OnceClosure trial) {
  if (IsTrialScheduled()) {
    LOG(WARNING) << "The previous scheduled trial hasn't been executed yet";
    return false;
  }

  trial_ = std::move(trial);
  dispatcher_->PostDelayedTask(FROM_HERE,
                               base::BindOnce(&TrialScheduler::ExecuteTrial,
                                              weak_ptr_factory_.GetWeakPtr()),
                               GetNextTrialDelay());
  return true;
}

void TrialScheduler::ExecuteTrial() {
  if (!trial_.is_null()) {
    UpdateNextInterval();
    last_trial_start_time_ = base::TimeTicks::Now();
    std::move(trial_).Run();
  }
}

void TrialScheduler::CancelTrial() {
  trial_.Reset();
  weak_ptr_factory_.InvalidateWeakPtrs();
}

bool TrialScheduler::IsTrialScheduled() const {
  return !trial_.is_null();
}

void TrialScheduler::ResetInterval() {
  next_interval_ = base::TimeDelta();
}

void TrialScheduler::UpdateNextInterval() {
  if (next_interval_.is_zero()) {
    next_interval_ = kBaseInterval;
  } else if (next_interval_ < kMaxInterval) {
    next_interval_ = std::min(next_interval_ * 2, kMaxInterval);
  }
}

base::TimeDelta TrialScheduler::GetNextTrialDelay() const {
  if (next_interval_.is_zero()) {
    return base::TimeDelta();
  }

  const base::TimeTicks next_attempt = last_trial_start_time_ + next_interval_;
  return std::max(next_attempt - base::TimeTicks::Now(), kMinDelay);
}

}  // namespace shill
