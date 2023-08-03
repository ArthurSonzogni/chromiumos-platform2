// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SWAP_MANAGEMENT_SWAP_TOOL_METRICS_H_
#define SWAP_MANAGEMENT_SWAP_TOOL_METRICS_H_

#include <absl/status/status.h>

#include "metrics/metrics_library.h"

namespace swap_management {

class SwapToolMetrics {
 public:
  static SwapToolMetrics* Get();

  void ReportSwapStartStatus(absl::Status status);
  void ReportSwapStopStatus(absl::Status status);

 private:
  SwapToolMetrics() = default;
  SwapToolMetrics& operator=(const SwapToolMetrics&) = delete;
  SwapToolMetrics(const SwapToolMetrics&) = delete;

  ~SwapToolMetrics() = default;

  MetricsLibrary metrics_;
};
}  // namespace swap_management

#endif  // SWAP_MANAGEMENT_SWAP_TOOL_METRICS_H_
