// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hps/hps_metrics.h"

namespace hps {

constexpr int kHpsUpdateMcuMaxDurationMilliSeconds = 60 * 1000;
constexpr int kHpsUpdateSpiMaxDurationMilliSeconds = 40 * 60 * 1000;

HpsMetrics::HpsMetrics() : metrics_lib_(std::make_unique<MetricsLibrary>()) {}

bool HpsMetrics::SendHpsTurnOnResult(HpsTurnOnResult result) {
  return metrics_lib_->SendEnumToUMA(hps::kHpsTurnOnResult, result);
}

bool HpsMetrics::SendHpsUpdateDuration(HpsBank bank, base::TimeDelta duration) {
  switch (bank) {
    case HpsBank::kMcuFlash:
      return metrics_lib_->SendToUMA(
          kHpsUpdateMcuDuration, static_cast<int>(duration.InMilliseconds()), 1,
          kHpsUpdateMcuMaxDurationMilliSeconds, 50);
    // The bank here is kSpiFlash, but the timing is for both kSpiFlash and
    // kSocRom
    case HpsBank::kSpiFlash:
      return metrics_lib_->SendToUMA(
          kHpsUpdateSpiDuration, static_cast<int>(duration.InMilliseconds()), 1,
          kHpsUpdateSpiMaxDurationMilliSeconds, 50);
    case HpsBank::kSocRom:
      break;
  }
  return true;
}

}  // namespace hps
