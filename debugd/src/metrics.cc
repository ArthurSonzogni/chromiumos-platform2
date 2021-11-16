// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "debugd/src/metrics.h"

#include <memory>
#include <string>

#include <base/time/time.h>
#include <metrics/metrics_library.h>

namespace debugd {

namespace {

// Histogram specifications
const char kHistogramPrefix[] = "ChromeOS.Debugd.";
const base::TimeDelta kHistogramMin = base::TimeDelta::FromMinutes(0);
const base::TimeDelta kHistogramMax = base::TimeDelta::FromMinutes(2);
const int kNumBuckets = 50;

}  // namespace

Stopwatch::Stopwatch(const std::string& histogram_postfix) {
  start_ = base::TimeTicks::Now();
  metrics_library_ = std::make_unique<MetricsLibrary>();
  histogram_name_ = kHistogramPrefix + histogram_postfix;
}

Stopwatch::~Stopwatch() {
  base::TimeDelta duration = base::TimeTicks::Now() - start_;
  metrics_library_->SendToUMA(histogram_name_, duration.InMilliseconds(),
                              kHistogramMin.InMilliseconds(),
                              kHistogramMax.InMilliseconds(), kNumBuckets);
}

}  // namespace debugd
