// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "secanomalyd/metrics.h"

#include <base/logging.h>
#include <base/macros.h>

#include <metrics/metrics_library.h>

namespace {

const char kSecurityAnomalyHistogramName[] = "ChromeOS.SecurityAnomaly";
const int kSecurityAnomalyHistogramMax = 50;

MetricsLibraryInterface* metrics_library = NULL;

void InitializeMetricsIfNecessary() {
  if (!metrics_library) {
    metrics_library = new MetricsLibrary();
  }
}

}  // namespace

bool SendSecurityAnomalyToUMA(SecurityAnomaly secanomaly) {
  InitializeMetricsIfNecessary();
  return metrics_library->SendEnumToUMA(kSecurityAnomalyHistogramName,
                                        static_cast<int>(secanomaly),
                                        kSecurityAnomalyHistogramMax);
}
