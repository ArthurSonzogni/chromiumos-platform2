// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "swap_management/swap_tool_metrics.h"

namespace swap_management {

namespace {
SwapToolMetrics* g_metrics_ = nullptr;

// Metrics file needs to be in stateful partition since it could be replayed in
// next boot time.
constexpr char kSwapMetricsFile[] = "/var/lib/swap/swap_metrics";
constexpr char kSwapStartStatus[] = "ChromeOS.SwapManagement.SwapStart.Status";
constexpr char kSwapStopStatus[] = "ChromeOS.SwapManagement.SwapStop.Status";
constexpr uint32_t kNumAbslStatus = 21;
}  // namespace

SwapToolMetrics* SwapToolMetrics::Get() {
  [[maybe_unused]] static bool created = []() -> bool {
    if (!g_metrics_) {
      g_metrics_ = new SwapToolMetrics;
      g_metrics_->metrics_.SetOutputFile(kSwapMetricsFile);
    }
    return true;
  }();

  return g_metrics_;
}

void SwapToolMetrics::ReportSwapStartStatus(absl::Status status) {
  metrics_.SendEnumToUMA(kSwapStartStatus, static_cast<int>(status.code()),
                         kNumAbslStatus);
}

void SwapToolMetrics::ReportSwapStopStatus(absl::Status status) {
  metrics_.SendEnumToUMA(kSwapStopStatus, static_cast<int>(status.code()),
                         kNumAbslStatus);
}

}  // namespace swap_management
