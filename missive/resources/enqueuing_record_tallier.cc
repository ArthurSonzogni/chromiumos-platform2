// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/resources/enqueuing_record_tallier.h"

#include <atomic>
#include <cstdint>
#include <optional>
#include <utility>

#include <base/logging.h>
#include <base/sequence_checker.h>
#include <base/time/time.h>
#include <base/timer/timer.h>

#include "base/memory/weak_ptr.h"
#include "missive/proto/record.pb.h"
#include "missive/util/statusor.h"
#include "missive/util/time.h"

namespace reporting {

EnqueuingRecordTallier::EnqueuingRecordTallier(base::TimeDelta interval) {
  timer_.Start(FROM_HERE, interval,
               base::BindRepeating(&EnqueuingRecordTallier::UpdateAverage,
                                   weak_ptr_factory_.GetWeakPtr()));
}

EnqueuingRecordTallier::~EnqueuingRecordTallier() = default;

StatusOr<uint64_t> EnqueuingRecordTallier::GetCurrentWallTime() const {
  return GetCurrentTime(TimeType::kWall);
}

void EnqueuingRecordTallier::Tally(const Record& record) {
  cumulated_size_ += record.ByteSizeLong();
}

std::optional<uint64_t> EnqueuingRecordTallier::GetAverage() const {
  const uint64_t average = average_.load();
  if (average == kAverageNullOpt) {
    return std::nullopt;
  } else {
    return average;
  }
}

// static
void EnqueuingRecordTallier::UpdateAverage(
    base::WeakPtr<EnqueuingRecordTallier> self) {
  if (!self) {
    return;
  }
  DCHECK_CALLED_ON_VALID_SEQUENCE(self->sequence_checker_);
  if (auto average_status = self->ComputeAverage();
      average_status.has_value()) {
    self->average_ = average_status.value();
  } else {
    LOG(ERROR)
        << "The rate of new events (enqueuing events) cannot be computed: "
        << average_status.error();
    self->average_ = kAverageNullOpt;
  }
}

StatusOr<uint64_t> EnqueuingRecordTallier::ComputeAverage() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // Reset cumulated_size_ and last_wall_time_ here so no need to worry about
  // returning early.
  const uint64_t cumulated_size = cumulated_size_.exchange(0U);
  const auto last_wall_time = std::move(last_wall_time_);
  const auto wall_time = GetCurrentWallTime();
  last_wall_time_ = wall_time;

  // If either current wall time or last wall time is missing, return an error.
  if (!wall_time.has_value()) {
    return wall_time;
  }
  if (!last_wall_time.has_value()) {
    return last_wall_time;
  }

  // Both wall times were obtained. Return the average rate of enqueuing
  // records.
  // wall_time is expected to be no earlier than last_wall_time. No CHECK should
  // be added here because the system time may be adjusted. If last_wall_time is
  // earlier than wall_time, the time difference would be conservatively treated
  // as 1, which is consistent with the browser's treatment in case of
  // unavailable average.
  //
  // We don't use a simple max operator here because of the unsignedness of the
  // variables.
  const uint64_t time_elapsed =
      (wall_time.value() <= last_wall_time.value())
          ? 1U
          : (wall_time.value() - last_wall_time.value());
  return cumulated_size / time_elapsed;
}
}  // namespace reporting
