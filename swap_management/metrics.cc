// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "swap_management/metrics.h"

namespace swap_management {

namespace {
constexpr char kSwapStartStatus[] = "ChromeOS.SwapManagement.SwapStart.Status";
constexpr char kSwapStopStatus[] = "ChromeOS.SwapManagement.SwapStop.Status";
constexpr uint32_t kNumAbslStatus = 21;
}  // namespace

Metrics* Metrics::Get() {
  return *GetSingleton<Metrics>();
}

void Metrics::ReportSwapStartStatus(absl::Status status) {
  metrics_.SendEnumToUMA(kSwapStartStatus, static_cast<int>(status.code()),
                         kNumAbslStatus);
}

void Metrics::ReportSwapStopStatus(absl::Status status) {
  metrics_.SendEnumToUMA(kSwapStopStatus, static_cast<int>(status.code()),
                         kNumAbslStatus);
}

}  // namespace swap_management
