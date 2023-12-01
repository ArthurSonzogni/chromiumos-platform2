// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SWAP_MANAGEMENT_METRICS_H_
#define SWAP_MANAGEMENT_METRICS_H_

#include "metrics/metrics_library.h"
#include "swap_management/utils.h"

#include <absl/status/status.h>

namespace swap_management {

class Metrics {
 public:
  static Metrics* Get();

  void ReportSwapStartStatus(absl::Status status);
  void ReportSwapStopStatus(absl::Status status);

 private:
  Metrics() = default;
  Metrics& operator=(const Metrics&) = delete;
  Metrics(const Metrics&) = delete;

  ~Metrics() = default;

  friend Metrics** GetSingleton<Metrics>();

  MetricsLibrary metrics_;
};
}  // namespace swap_management

#endif  // SWAP_MANAGEMENT_METRICS_H_
