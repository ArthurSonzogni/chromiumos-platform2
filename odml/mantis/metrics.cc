// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/mantis/metrics.h"

#include <string>

#include <base/logging.h>
#include <base/time/time.h>
#include <metrics/metrics_library.h>

namespace mantis {
namespace {

// Structure to hold information about a specific time metric.
struct TimeMetricInfo {
  // The name of the metric as a string, used for reporting.
  std::string string_name;
  // The maximum expected value for the metric.
  base::TimeDelta max;
  // The minimum expected value for the metric.
  base::TimeDelta min = base::Milliseconds(1);
  // Number of buckets to use for histogram
  int num_buckets = 50;
};

TimeMetricInfo GetMetricInfo(TimeMetric metric) {
  switch (metric) {
    case TimeMetric::kLoadModelLatency:
      return {
          .string_name = "Platform.MantisService.Latency.LoadModel",
          .max = base::Seconds(30),
      };
    case TimeMetric::kInpaintingLatency:
      return {
          .string_name = "Platform.MantisService.Latency.Inpainting",
          .max = base::Seconds(30),
      };
    case TimeMetric::kOutpaintingLatency:
      return {
          .string_name = "Platform.MantisService.Latency.Outpainting",
          .max = base::Seconds(30),
      };
    case TimeMetric::kGenerativeFillLatency:
      return {
          .string_name = "Platform.MantisService.Latency.GenerativeFill",
          .max = base::Seconds(30),
      };
    case TimeMetric::kSegmentationLatency:
      return {
          .string_name = "Platform.MantisService.Latency.Segmentation",
          .max = base::Seconds(30),
      };
    case TimeMetric::kClassifyImageSafetyLatency:
      return {
          .string_name = "Platform.MantisService.Latency.ClassifyImageSafety",
          .max = base::Seconds(30),
      };
  }
}

std::string GetMetricName(BoolMetric metric) {
  switch (metric) {
    case BoolMetric::kModelLoaded:
      return "Platform.MantisService.ModelLoaded";
  }
}

std::string GetMetricName(EnumMetric metric) {
  switch (metric) {
    case mantis::EnumMetric::kImageGenerationType:
      return "Platform.MantisService.ImageGenerationType";
  }
}

}  // namespace

void SendTimeMetric(MetricsLibraryInterface& metrics_lib,
                    TimeMetric metric,
                    const odml::PerformanceTimer& timer) {
  TimeMetricInfo info = GetMetricInfo(metric);
  metrics_lib.SendTimeToUMA(info.string_name, timer.GetDuration(), info.min,
                            info.max, info.num_buckets);
}

void SendBoolMetric(MetricsLibraryInterface& metrics_lib,
                    BoolMetric metric,
                    bool value) {
  metrics_lib.SendBoolToUMA(GetMetricName(metric), value);
}

void SendImageGenerationTypeMetric(MetricsLibraryInterface& metrics_lib,
                                   ImageGenerationType type) {
  metrics_lib.SendEnumToUMA(GetMetricName(EnumMetric::kImageGenerationType),
                            type);
}

}  // namespace mantis
