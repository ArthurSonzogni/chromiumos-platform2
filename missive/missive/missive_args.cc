// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/missive/missive_args.h"

#include <cstdlib>
#include <string>

#include <base/logging.h>
#include <base/strings/string_piece.h>
#include <base/time/time.h>
#include <base/time/time_delta_from_string.h>
#include <brillo/flag_helper.h>

#include "missive/util/statusor.h"

namespace reporting {
namespace {

// Parses duration. If the parsed duration is invalid.
StatusOr<base::TimeDelta> ParseDuration(base::StringPiece duration_string) {
  const auto duration_result = base::TimeDeltaFromString(duration_string);
  if (!duration_result.has_value()) {
    return Status(error::INVALID_ARGUMENT, "Duration is not parseable.");
  }
  if (!duration_result.value().is_positive()) {
    return Status(error::INVALID_ARGUMENT, "Duration is not positive.");
  }
  return duration_result.value();
}

// Parses duration_string if valid. Otherwise, parses duration_default, which
// should always be valid.
base::TimeDelta DurationParameterValue(base::StringPiece arg_name,
                                       base::StringPiece duration_string,
                                       base::StringPiece duration_default) {
  DCHECK(ParseDuration(duration_default).ok());

  const auto duration_result = ParseDuration(duration_string);
  if (!duration_result.ok()) {
    LOG(ERROR) << "Unable to parse argument " << arg_name << "="
               << duration_string << ", assumed default=" << duration_default
               << ", because: " << duration_result.status();
    return ParseDuration(duration_default).ValueOrDie();
  }
  return duration_result.ValueOrDie();
}
}  // namespace

MissiveArgs::MissiveArgs(base::StringPiece enqueuing_record_tallier,
                         base::StringPiece cpu_collector_interval,
                         base::StringPiece storage_collector_interval,
                         base::StringPiece memory_collector_interval)
    : enqueuing_record_tallier_(
          DurationParameterValue("enqueuing_record_tallier",
                                 enqueuing_record_tallier,
                                 kEnqueuingRecordTallierDefault)),
      cpu_collector_interval_(
          DurationParameterValue("cpu_collector_interval",
                                 cpu_collector_interval,
                                 kCpuCollectorIntervalDefault)),
      storage_collector_interval_(
          DurationParameterValue("storage_collector_interval",
                                 storage_collector_interval,
                                 kStorageCollectorIntervalDefault)),
      memory_collector_interval_(
          DurationParameterValue("memory_collector_interval",
                                 memory_collector_interval,
                                 kMemoryCollectorIntervalDefault)) {}

MissiveArgs::~MissiveArgs() = default;

}  // namespace reporting
