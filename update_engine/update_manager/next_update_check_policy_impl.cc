// Copyright 2017 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/update_manager/next_update_check_policy_impl.h"

#include <algorithm>

#include <base/logging.h>

#include "update_engine/common/utils.h"
#include "update_engine/update_manager/policy_utils.h"

using base::Time;
using base::TimeDelta;
using std::max;
using std::string;

namespace chromeos_update_manager {

const NextUpdateCheckPolicyConstants kNextUpdateCheckPolicyConstants = {
    .timeout_initial_interval = 7 * 60,
    .timeout_periodic_interval = 45 * 60,
    .timeout_max_backoff_interval = 4 * 60 * 60,
    .timeout_regular_fuzz = 10 * 60,
    .attempt_backoff_max_interval_in_days = 16,
    .attempt_backoff_fuzz_in_hours = 12,
};

NextUpdateCheckTimePolicyImpl::NextUpdateCheckTimePolicyImpl()
    : NextUpdateCheckTimePolicyImpl(kNextUpdateCheckPolicyConstants) {}

NextUpdateCheckTimePolicyImpl::NextUpdateCheckTimePolicyImpl(
    const NextUpdateCheckPolicyConstants& constants)
    : policy_constants_(constants) {}

EvalStatus NextUpdateCheckTimePolicyImpl::Evaluate(
    EvaluationContext* ec,
    State* state,
    string* error,
    PolicyDataInterface* data) const {
  // Ensure that periodic update checks are timed properly.
  Time next_update_check;

  if (NextUpdateCheckTime(ec, state, error, &next_update_check,
                          policy_constants_) != EvalStatus::kSucceeded) {
    return EvalStatus::kFailed;
  }
  if (!ec->IsWallclockTimeGreaterThan(next_update_check)) {
    LOG(INFO) << "Periodic check interval not satisfied, blocking until "
              << chromeos_update_engine::utils::ToString(next_update_check);
    return EvalStatus::kAskMeAgainLater;
  }

  return EvalStatus::kContinue;
}

EvalStatus NextUpdateCheckTimePolicyImpl::NextUpdateCheckTime(
    EvaluationContext* ec,
    State* state,
    string* error,
    Time* next_update_check,
    const NextUpdateCheckPolicyConstants& constants) {
  UpdaterProvider* const updater_provider = state->updater_provider();

  // Don't check for updates too often. We limit the update checks to once every
  // some interval. The interval is kTimeoutInitialInterval the first time and
  // kTimeoutPeriodicInterval for the subsequent update checks. If the update
  // check fails, we increase the interval between the update checks
  // exponentially until kTimeoutMaxBackoffInterval. Finally, to avoid having
  // many chromebooks running update checks at the exact same time, we add some
  // fuzz to the interval.
  const Time* updater_started_time =
      ec->GetValue(updater_provider->var_updater_started_time());
  POLICY_CHECK_VALUE_AND_FAIL(updater_started_time, error);

  // This value is used for testing only and it will get deleted after the first
  // time it is read.
  const int64_t* interval_timeout =
      ec->GetValue(updater_provider->var_test_update_check_interval_timeout());

  const Time* last_checked_time =
      ec->GetValue(updater_provider->var_last_checked_time());

  const auto* seed = ec->GetValue(state->random_provider()->var_seed());
  POLICY_CHECK_VALUE_AND_FAIL(seed, error);

  PRNG prng(*seed);

  // If this is the first attempt, compute and return an initial value.
  if (last_checked_time == nullptr ||
      *last_checked_time < *updater_started_time) {
    TimeDelta time_diff =
        interval_timeout == nullptr
            ? FuzzedInterval(&prng, constants.timeout_initial_interval,
                             constants.timeout_regular_fuzz)
            : base::Seconds(*interval_timeout);
    *next_update_check = *updater_started_time + time_diff;
    return EvalStatus::kSucceeded;
  }

  if (interval_timeout != nullptr) {
    *next_update_check = *last_checked_time + base::Seconds(*interval_timeout);
    return EvalStatus::kSucceeded;
  }
  // Check whether the server is enforcing a poll interval; if not, this value
  // will be zero.
  const unsigned int* server_dictated_poll_interval =
      ec->GetValue(updater_provider->var_server_dictated_poll_interval());
  POLICY_CHECK_VALUE_AND_FAIL(server_dictated_poll_interval, error);

  int interval = *server_dictated_poll_interval;
  int fuzz = 0;

  // If no poll interval was dictated by server compute a back-off period,
  // starting from a predetermined base periodic interval and increasing
  // exponentially by the number of consecutive failed attempts.
  if (interval == 0) {
    const unsigned int* consecutive_failed_update_checks =
        ec->GetValue(updater_provider->var_consecutive_failed_update_checks());
    POLICY_CHECK_VALUE_AND_FAIL(consecutive_failed_update_checks, error);

    interval = constants.timeout_periodic_interval;
    unsigned int num_failures = *consecutive_failed_update_checks;
    while (interval < constants.timeout_max_backoff_interval && num_failures) {
      interval *= 2;
      num_failures--;
    }
  }

  // We cannot back off longer than the predetermined maximum interval.
  if (interval > constants.timeout_max_backoff_interval) {
    interval = constants.timeout_max_backoff_interval;
  }

  // We cannot back off shorter than the predetermined periodic interval. Also,
  // in this case set the fuzz to a predetermined regular value.
  if (interval <= constants.timeout_periodic_interval) {
    interval = constants.timeout_periodic_interval;
    fuzz = constants.timeout_regular_fuzz;
  }

  // If not otherwise determined, defer to a fuzz of +/-(interval / 2).
  if (fuzz == 0) {
    fuzz = interval;
  }

  *next_update_check =
      *last_checked_time + FuzzedInterval(&prng, interval, fuzz);
  return EvalStatus::kSucceeded;
}

TimeDelta NextUpdateCheckTimePolicyImpl::FuzzedInterval(PRNG* prng,
                                                        int interval,
                                                        int fuzz) {
  DCHECK_GE(interval, 0);
  DCHECK_GE(fuzz, 0);
  int half_fuzz = fuzz / 2;
  // This guarantees the output interval is non negative.
  int interval_min = max(interval - half_fuzz, 0);
  int interval_max = interval + half_fuzz;
  return base::Seconds(prng->RandMinMax(interval_min, interval_max));
}

}  // namespace chromeos_update_manager
