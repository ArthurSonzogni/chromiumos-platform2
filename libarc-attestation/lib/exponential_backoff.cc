// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include <base/functional/callback.h>
#include <libarc-attestation/lib/exponential_backoff.h>

namespace arc_attestation {

ExponentialBackoff::ExponentialBackoff(
    double starting_delay,
    double multiplier,
    base::RepeatingCallback<bool()> try_callback,
    scoped_refptr<base::SequencedTaskRunner> runner)
    : starting_delay_(starting_delay),
      multiplier_(multiplier),
      try_counter_(0),
      try_callback_(std::move(try_callback)),
      runner_(runner) {}

void ExponentialBackoff::TriggerTry() {
  bool ret = try_callback_.Run();
  if (ret) {
    // We're good, but we need to increase the counter to avoid any in-flight
    // delay.
    try_counter_++;
    return;
  }

  current_delay_ = starting_delay_;
  ScheduleDelayedTry();
}

void ExponentialBackoff::OnTimesUp(int counter) {
  if (counter != try_counter_) {
    // This callback has been cancelled.
    return;
  }

  // Give the callback a try.
  bool ret = try_callback_.Run();
  if (ret) {
    // We're good.
    return;
  }

  // Failed, need to retry.
  current_delay_ = current_delay_ * multiplier_;
  ScheduleDelayedTry();
}

void ExponentialBackoff::ScheduleDelayedTry() {
  try_counter_++;
  int counter = try_counter_;
  runner_->PostDelayedTask(FROM_HERE,
                           base::BindOnce(&ExponentialBackoff::OnTimesUp,
                                          base::Unretained(this), counter),
                           base::Milliseconds(current_delay_));
}

}  // namespace arc_attestation
