// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HPS_HPS_METRICS_H_
#define HPS_HPS_METRICS_H_

#include <memory>
#include <utility>

#include <metrics/metrics_library.h>

namespace hps {

constexpr char kHpsTurnOnResult[] = "ChromeOS.HPS.TurnOn.Result";

// These values are logged to UMA. Entries should not be renumbered and
// numeric values should never be reused. Please keep in sync with
// "HpsTurnOnResult" in tools/metrics/histograms/enums.xml in the Chromium repo.
enum class HpsTurnOnResult {
  kSuccess = 0,
  kMcuVersionMismatch = 1,
  kSpiNotVerified = 2,
  kMcuNotVerified = 3,
  kStage1NotStarted = 4,
  kApplNotStarted = 5,
  kNoResponse = 6,
  kTimeout = 7,
  kBadMagic = 8,
  kFault = 9,
  kMaxValue = kFault,
};

class HpsMetrics {
 public:
  HpsMetrics();
  HpsMetrics(const HpsMetrics&) = delete;
  HpsMetrics& operator=(const HpsMetrics&) = delete;

  ~HpsMetrics() = default;

  bool SendHpsTurnOnResult(HpsTurnOnResult result);

  void SetMetricsLibraryForTesting(
      std::unique_ptr<MetricsLibraryInterface> metrics_lib) {
    metrics_lib_ = std::move(metrics_lib);
  }

  MetricsLibraryInterface* metrics_library_for_testing() {
    return metrics_lib_.get();
  }

 private:
  std::unique_ptr<MetricsLibraryInterface> metrics_lib_;
};

}  // namespace hps

#endif  // HPS_HPS_METRICS_H_
