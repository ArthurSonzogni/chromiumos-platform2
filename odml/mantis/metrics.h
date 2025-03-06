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
  kOutpaintingLatency,
  kSegmentationLatency,
  kClassifyImageSafetyLatency,
};

// Enum representing different bool-based metrics to be tracked.
enum class BoolMetric {
  kModelLoaded,
};

// Enum representing different enum-based metrics to be tracked.
enum class EnumMetric {
  kImageGenerationType,
};

// Enum representing types of image generation operation in Mantis.
// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum class ImageGenerationType {
  kInpainting = 0,
  kGenerativeFill = 1,
  KOutpainting = 2,
  kMaxValue = KOutpainting,
};

// Sends a time metric with the elapsed duration from the provided timer.
void SendTimeMetric(MetricsLibraryInterface& metrics_lib,
                    TimeMetric metric,
                    const odml::PerformanceTimer& timer);

// Sends a bool metric with the given `value`.
void SendBoolMetric(MetricsLibraryInterface& metrics_lib,
                    BoolMetric metric,
                    bool value);

// Sends the generated image type.
void SendImageGenerationTypeMetric(MetricsLibraryInterface& metrics_lib,
                                   ImageGenerationType type);

}  // namespace mantis

#endif  // ODML_MANTIS_METRICS_H_
