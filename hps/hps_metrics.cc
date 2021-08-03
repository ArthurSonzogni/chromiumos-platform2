// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hps/hps_metrics.h"

namespace hps {

HpsMetrics::HpsMetrics() : metrics_lib_(std::make_unique<MetricsLibrary>()) {}

bool HpsMetrics::SendHpsTurnOnResult(HpsTurnOnResult result) {
  return metrics_lib_->SendEnumToUMA(hps::kHpsTurnOnResult, result);
}

}  // namespace hps
