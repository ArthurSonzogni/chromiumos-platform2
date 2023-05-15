// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/vmm_swap_usage_policy.h"

#include <algorithm>
#include <optional>
#include <utility>

#include <base/logging.h>
#include <base/time/time.h>

namespace vm_tools::concierge {

namespace {
constexpr base::TimeDelta WEEK = base::Days(7);
}  // namespace

void VmmSwapUsagePolicy::OnEnabled(base::Time time) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (is_enabled_) {
    return;
  }
  is_enabled_ = true;

  if (usage_history_.CurrentIndex() == 0 ||
      usage_history_.ReadBuffer(usage_history_.BufferSize() - 1).start <=
          time - base::Hours(1)) {
    struct SwapPeriod entry;
    entry.start = time;
    entry.duration.reset();
    usage_history_.SaveToBuffer(entry);
  }
}

void VmmSwapUsagePolicy::OnDisabled(base::Time time) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  AddEnableRecordIfMissing(time);

  if (!is_enabled_) {
    return;
  }
  is_enabled_ = false;

  auto latest_entry =
      usage_history_.MutableReadBuffer(usage_history_.BufferSize() - 1);
  if (latest_entry->start > time) {
    LOG(WARNING) << "Time mismatch: (enabled) " << latest_entry->start
                 << " > (disabled) " << time;
  } else if (!latest_entry->duration.has_value()) {
    latest_entry->duration = time - latest_entry->start;
  }
}

base::TimeDelta VmmSwapUsagePolicy::PredictDuration(base::Time now) {
  // Predict when vmm-swap is disabled by averaging the last 4 weeks log.
  // If this has less than 1 week log, this estimates to be disabled after the
  // double length of the latest enabled duration.
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  AddEnableRecordIfMissing(now);

  if (usage_history_.CurrentIndex() == 0) {
    // There are no data.
    return base::TimeDelta();
  }

  base::TimeDelta sum = base::TimeDelta();
  int num_weeks_to_count = (now - usage_history_.Begin()->start).IntDiv(WEEK);
  if (num_weeks_to_count > kUsageHistoryNumWeeks) {
    num_weeks_to_count = kUsageHistoryNumWeeks;
  }
  if (num_weeks_to_count == 0) {
    // There is less than 1 week data.
    auto latest_entry =
        usage_history_.ReadBuffer(usage_history_.BufferSize() - 1);
    return latest_entry.duration.value_or(now - latest_entry.start) * 2;
  }
  for (auto iter = usage_history_.Begin(); iter; ++iter) {
    base::TimeDelta duration = iter->duration.value_or(now - iter->start);

    int64_t start_weeks_ago = std::min((now - iter->start).IntDiv(WEEK),
                                       (int64_t)kUsageHistoryNumWeeks);
    int64_t end_weeks_ago = (now - (iter->start + duration)).IntDiv(WEEK);

    // The record which is across the projected time of the week is used for the
    // prediction.
    if (end_weeks_ago < kUsageHistoryNumWeeks &&
        start_weeks_ago != end_weeks_ago) {
      base::Time projected_time = now - WEEK * start_weeks_ago;
      base::TimeDelta duration_of_week =
          duration + iter->start - projected_time;
      sum += duration_of_week;
      while (duration_of_week > WEEK) {
        duration_of_week -= WEEK;
        sum += duration_of_week;
      }
    }
  }

  return sum / num_weeks_to_count;
}

// Enable record can be skipped if it is enabled again within 1 hour. However if
// it is disabled after more than 1 hour, a new record should be added to the
// history. The time enabled is between `latest_entry->start` and 1 hour later.
// We use `latest_entry->start` + 1 hour pessimistically as the enabled time of
// the new record.
void VmmSwapUsagePolicy::AddEnableRecordIfMissing(base::Time time) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!is_enabled_) {
    return;
  }
  auto latest_entry =
      usage_history_.ReadBuffer(usage_history_.BufferSize() - 1);
  if (latest_entry.duration.has_value() &&
      (time - latest_entry.start) >= base::Hours(1)) {
    struct SwapPeriod entry;
    entry.start = latest_entry.start + base::Hours(1);
    entry.duration.reset();
    usage_history_.SaveToBuffer(entry);
  }
}

}  // namespace vm_tools::concierge
