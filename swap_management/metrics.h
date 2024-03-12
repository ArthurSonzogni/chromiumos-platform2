// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SWAP_MANAGEMENT_METRICS_H_
#define SWAP_MANAGEMENT_METRICS_H_

#include "metrics/metrics_library.h"
#include "swap_management/utils.h"

#include <vector>

#include <absl/status/status.h>
#include <base/timer/timer.h>

namespace swap_management {

class Metrics {
 public:
  static Metrics* Get();
  static void OverrideForTesting(Metrics* metrics);

  void ReportSwapStartStatus(absl::Status status);
  void ReportSwapStopStatus(absl::Status status);
  void PeriodicReportZramMetrics();
  void Stop();

  // Virtual for testing.
  virtual void Start();

  // Parse /proc/pressure/memory and return {psi_some_in_period,
  // psi_full_in_period} pair in decimal if success. |period| can only be 10, 60
  // or 300.
  absl::StatusOr<std::vector<uint32_t>> PSIMemoryParser(uint32_t period);

 private:
  Metrics() = default;
  Metrics& operator=(const Metrics&) = delete;
  Metrics(const Metrics&) = delete;

  virtual ~Metrics();

  friend Metrics** GetSingleton<Metrics>();
  friend class MockMetrics;

  base::WeakPtrFactory<Metrics> weak_factory_{this};
  MetricsLibrary metrics_;

  base::RepeatingTimer metrics_timer_;

  // For tracking zram huge pages metric.
  bool has_old_huge_pages_ = false;
  uint64_t old_huge_pages_ = 0;
  uint64_t old_huge_pages_since_ = 0;
};
}  // namespace swap_management

#endif  // SWAP_MANAGEMENT_METRICS_H_
