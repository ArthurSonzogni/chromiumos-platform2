// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_MANTIS_METRICS_H_
#define ODML_MANTIS_METRICS_H_

#include <base/logging.h>
#include <base/time/time.h>
#include <metrics/metrics_library.h>

#include "odml/utils/performance_timer.h"

namespace mantis {

// Enum representing different time-based metrics to be tracked.
enum class TimeMetric {
  kLoadModelLatency,
  kInpaintingLatency,
  kGenerativeFillLatency,
  kSegmentationLatency,
  kClassifyImageSafetyLatency,
};

// Sends a time metric with the elapsed duration from the provided timer.
void SendTimeMetric(MetricsLibraryInterface& metrics_lib,
                    TimeMetric metric,
                    const odml::PerformanceTimer& timer);

}  // namespace mantis

#endif  // ODML_MANTIS_METRICS_H_
