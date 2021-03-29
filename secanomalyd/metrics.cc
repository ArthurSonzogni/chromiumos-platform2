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

const char* kSecurityAnomalyNames[] = {
    "Mount.InitNS.WX"  // 0
};

MetricsLibraryInterface* metrics_library = NULL;

void InitializeMetricsIfNecessary() {
  if (!metrics_library) {
    metrics_library = new MetricsLibrary();
  }
}

// Update this to be last entry + 1 when you add new entries to the end. Checks
// that no one tries to remove entries from the middle or misnumbers during a
// merge conflict.
static_assert(base::size(kSecurityAnomalyNames) == 1,
              "SecurityAnomaly enums not lining up properly");

}  // namespace

bool SendSecurityAnomalyToUMA(const std::string& anomaly) {
  InitializeMetricsIfNecessary();
  for (size_t i = 0; i < base::size(kSecurityAnomalyNames); i++) {
    if (strcmp(anomaly.c_str(), kSecurityAnomalyNames[i]) == 0) {
      return metrics_library->SendEnumToUMA(kSecurityAnomalyHistogramName, i,
                                            kSecurityAnomalyHistogramMax);
    }
  }
  LOG(WARNING) << "Unknown ChromeOS.SecurityAnomaly '" << anomaly << "'";
  return false;
}
