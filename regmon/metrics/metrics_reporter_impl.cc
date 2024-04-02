// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "regmon/metrics/metrics_reporter_impl.h"

#include <metrics/metrics_library.h>

namespace regmon::metrics {

MetricsReporterImpl::MetricsReporterImpl(MetricsLibraryInterface& metrics_lib)
    : metrics_lib_(&metrics_lib) {}

bool MetricsReporterImpl::ReportAnnotationViolation(int unique_id) {
  return metrics_lib_->SendSparseToUMA(
      "NetworkAnnotationMonitor.PolicyViolation", unique_id);
}

}  // namespace regmon::metrics
