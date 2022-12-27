// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/logging.h>

#include "diagnostics/cros_minidiag/minidiag_metrics.h"
#include "diagnostics/cros_minidiag/minidiag_metrics_names.h"

namespace cros_minidiag {

MiniDiagMetrics::MiniDiagMetrics() = default;
MiniDiagMetrics::~MiniDiagMetrics() = default;

void MiniDiagMetrics::RecordLaunch(int count) const {
  if (!metrics_library_->SendLinearToUMA(metrics::kLaunchHistogram, count,
                                         metrics::kLaunchCountMax))
    LOG(ERROR) << "Cannot send MiniDiag launch count to UMA";
}

}  // namespace cros_minidiag
