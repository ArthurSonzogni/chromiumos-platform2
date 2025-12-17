// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "swap_management/zram_idle.h"

#include <base/logging.h>

#include "swap_management/utils.h"

namespace swap_management {

absl::Status MarkIdle(uint32_t age_seconds) {
  base::FilePath filepath = base::FilePath(kZramSysfsDir).Append("idle");
  return Utils::Get()->WriteFile(filepath, std::to_string(age_seconds));
}

uint64_t GetCurrentIdleTimeSec(uint64_t min_sec, uint64_t max_sec) {
  absl::StatusOr<base::SystemMemoryInfo> meminfo =
      Utils::Get()->GetSystemMemoryInfo();
  if (!meminfo.ok()) {
    LOG(ERROR) << "Can not read meminfo: " << meminfo.status();
    // Fallback to use the safest value.
    return max_sec;
  }

  // Stay between idle_(min|max)_time.
  double mem_utilization =
      1.0 - (*meminfo).available.InKiBF() / (*meminfo).total.InKiBF();

  // Exponentially decay the age vs. memory utilization. The reason
  // we choose exponential decay is because we want to do as little work as
  // possible when the system is under very low memory pressure. As pressure
  // increases we want to start aggressively shrinking our idle age to force
  // newer pages to be written back/recompressed.
  constexpr double kLambda = 5;
  return (max_sec - min_sec) * pow(M_E, -kLambda * mem_utilization) + min_sec;
}

}  // namespace swap_management
