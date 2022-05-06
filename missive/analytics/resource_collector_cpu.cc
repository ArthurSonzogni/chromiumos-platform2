// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/analytics/resource_collector_cpu.h"

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <ctime>
#include <string>

#include <base/logging.h>
#include <base/strings/strcat.h>
#include <metrics/metrics_library.h>

#include "missive/util/statusor.h"

namespace reporting::analytics {

namespace {
std::string GetSystemErrorMessage() {
  static constexpr size_t kBufSize = 256U;
  std::string error_msg;
  error_msg.reserve(kBufSize);
  const char* error_str = strerror_r(errno, error_msg.data(), error_msg.size());
  if (error_str == nullptr) {
    error_msg = "Unknown error";
  }
  return error_msg;
}
}  // namespace

ResourceCollectorCpu::ResourceCollectorCpu(base::TimeDelta interval)
    : ResourceCollector(interval) {}

ResourceCollectorCpu::~ResourceCollectorCpu() = default;

void ResourceCollectorCpu::Collect() {
  const auto cpu_percentage = tallier_->Tally();
  if (!cpu_percentage.ok()) {
    LOG(ERROR) << cpu_percentage.status();
    return;
  }
  SendCpuUsagePercentageToUma(cpu_percentage.ValueOrDie());
}

bool ResourceCollectorCpu::SendCpuUsagePercentageToUma(
    uint64_t cpu_percentage) {
  return metrics_->SendPercentageToUMA(
      /*name=*/kUmaName,
      /*sample=*/static_cast<int>(cpu_percentage));
}

StatusOr<uint64_t> ResourceCollectorCpu::CpuUsageTallier::Tally()
    VALID_CONTEXT_REQUIRED(sequence_checker_) {
  struct timespec tp_cpu, tp_wall;
  // If we fail to retrieve either CPU or wall time, we give up for this round.
  if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &tp_cpu)) {
    return Status(error::UNKNOWN, base::StrCat({"Failed to retrieve CPU time: ",
                                                GetSystemErrorMessage()}));
  }
  if (clock_gettime(CLOCK_REALTIME, &tp_wall)) {
    return Status(error::UNKNOWN,
                  base::StrCat({"Failed to retrieve wall-clock time: ",
                                GetSystemErrorMessage()}));
  }

  // We ignore the nanosecond part because we don't need that level of accuracy.
  uint64_t result = static_cast<uint64_t>(tp_cpu.tv_sec - last_cpu_time_) *
                    100U /
                    static_cast<uint64_t>(tp_wall.tv_sec - last_wall_time_);

  // Update stored CPU time and wall time
  last_cpu_time_ = tp_cpu.tv_sec;
  last_wall_time_ = tp_wall.tv_sec;

  return result;
}

ResourceCollectorCpu::CpuUsageTallier::~CpuUsageTallier() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

}  // namespace reporting::analytics
