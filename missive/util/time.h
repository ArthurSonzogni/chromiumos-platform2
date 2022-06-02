// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MISSIVE_UTIL_TIME_H_
#define MISSIVE_UTIL_TIME_H_

#include <cstddef>

#include "missive/util/statusor.h"

namespace reporting {

enum class TimeType : uint8_t {
  kWall = 1,       // Real time, aka wall time
  kProcessCpu = 2  // CPU time used by the process
};

// Get the timestamp of the current time of the given type.
StatusOr<uint64_t> GetCurrentTime(TimeType type);

}  // namespace reporting

#endif  // MISSIVE_UTIL_TIME_H_
