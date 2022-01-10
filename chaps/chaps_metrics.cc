// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chaps/chaps_metrics.h"

#include <string>

#include <metrics/metrics_library.h>

namespace chaps {

void ChapsMetrics::ReportReinitializingTokenStatus(
    ReinitializingTokenStatus status) {
#ifndef NO_METRICS
  constexpr auto max_value =
      static_cast<int>(ReinitializingTokenStatus::kMaxValue);
  metrics_library_->SendEnumToUMA(kReinitializingToken,
                                  static_cast<int>(status), max_value);
#endif
}

void ChapsMetrics::ReportTPMAvailabilityStatus(TPMAvailabilityStatus status) {
#ifndef NO_METRICS
  constexpr auto max_value = static_cast<int>(TPMAvailabilityStatus::kMaxValue);
  metrics_library_->SendEnumToUMA(kTPMAvailability, static_cast<int>(status),
                                  max_value);
#endif
}

void ChapsMetrics::ReportCrosEvent(const std::string& event) {
#ifndef NO_METRICS
  metrics_library_->SendCrosEventToUMA(event);
#endif
}

}  // namespace chaps
