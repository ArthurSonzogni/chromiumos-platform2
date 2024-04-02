// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REGMON_METRICS_METRICS_REPORTER_IMPL_H_
#define REGMON_METRICS_METRICS_REPORTER_IMPL_H_

#include <memory>

#include <metrics/metrics_library.h>

#include "regmon/metrics/metrics_reporter.h"

namespace regmon::metrics {

class MetricsReporterImpl : public MetricsReporter {
 public:
  explicit MetricsReporterImpl(MetricsLibraryInterface& metrics_lib);
  MetricsReporterImpl(const MetricsReporterImpl&) = delete;
  MetricsReporterImpl& operator=(const MetricsReporterImpl&) = delete;

  bool ReportAnnotationViolation(int unique_id);

 private:
  MetricsLibraryInterface* metrics_lib_;
};
}  // namespace regmon::metrics

#endif  // REGMON_METRICS_METRICS_REPORTER_IMPL_H_
