// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBARC_ATTESTATION_LIB_EXPONENTIAL_BACKOFF_H_
#define LIBARC_ATTESTATION_LIB_EXPONENTIAL_BACKOFF_H_

#include <base/functional/callback_forward.h>
#include <base/memory/scoped_refptr.h>
#include <base/task/single_thread_task_runner.h>
#include <base/time/time.h>

namespace arc_attestation {

// ExponentialBackoff is a utility class for implementing cancellable
// exponential backoff retry. All time units are in ms.
class ExponentialBackoff {
 public:
  ExponentialBackoff(double starting_delay,
                     double multiplier,
                     base::RepeatingCallback<bool()> try_callback,
                     scoped_refptr<base::SequencedTaskRunner> runner);

  // Reset the exponential backoff and start a new try.
  void TriggerTry();

 private:
  // Called for next retry.
  void OnTimesUp(int counter);

  // Schedule the delayed task for next retry.
  void ScheduleDelayedTry();

  // How much delay at first?
  double starting_delay_;

  // How much longer the delay gets for each failure?
  double multiplier_;

  // How much delay between the last retry and the next retry?
  double current_delay_;

  // A counter used for cancelling or avoiding repeated retries.
  int try_counter_;

  // Call this for retry. Return true for success.
  base::RepeatingCallback<bool()> try_callback_;

  // Task runner for scheduling stuffs.
  scoped_refptr<base::SequencedTaskRunner> runner_;
};

}  // namespace arc_attestation

#endif  // LIBARC_ATTESTATION_LIB_EXPONENTIAL_BACKOFF_H_
