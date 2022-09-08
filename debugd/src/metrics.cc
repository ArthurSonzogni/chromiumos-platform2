// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "debugd/src/metrics.h"

#include <memory>
#include <string>

#include <base/logging.h>
#include <base/time/time.h>
#include <metrics/metrics_library.h>

namespace debugd {

namespace {

// Histogram specifications
const char kMetricPrefix[] = "ChromeOS.Debugd.";
const base::TimeDelta kHistogramMin = base::Minutes(0);
const base::TimeDelta kHistogramMax = base::Minutes(2);
const int kNumBuckets = 50;

}  // namespace

Stopwatch::Stopwatch(const std::string& metric_postfix,
                     const bool local_logging,
                     const bool report_to_uma)
    : local_logging_(local_logging), report_to_uma_(report_to_uma) {
  start_ = base::TimeTicks::Now();
  metric_name_ = kMetricPrefix + metric_postfix;
  if (report_to_uma_)
    metrics_library_ = std::make_unique<MetricsLibrary>();
}

void Stopwatch::Lap(const std::string& lap_name) {
  if (local_logging_) {
    base::TimeDelta lap_duration = base::TimeTicks::Now() - start_;
    DLOG(INFO) << metric_name_ << ", " << lap_name << ": " << lap_duration;
  }
}

Stopwatch::~Stopwatch() {
  base::TimeDelta duration = base::TimeTicks::Now() - start_;
  if (local_logging_)
    DLOG(INFO) << metric_name_ << ", total elapsed time: " << duration;
  if (metrics_library_)
    metrics_library_->SendToUMA(metric_name_, duration.InMilliseconds(),
                                kHistogramMin.InMilliseconds(),
                                kHistogramMax.InMilliseconds(), kNumBuckets);
}

}  // namespace debugd
