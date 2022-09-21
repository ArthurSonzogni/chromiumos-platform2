// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <metrics/metrics_library.h>

#include "installer/metrics.h"

// This is a "real" metrics implementation,
// which just passes through to libmetrics.
class Metrics : public MetricsInterface {
 public:
  Metrics() = default;

  Metrics(const Metrics&) = delete;
  Metrics& operator=(const Metrics&) = delete;

  bool SendBooleanMetric(const std::string& name, bool sample) override {
    return metrics_library_.SendBoolToUMA(name, sample);
  }
  bool SendEnumMetric(const std::string& name, int sample, int max) override {
    return metrics_library_.SendEnumToUMA(name, sample, max);
  }

 private:
  MetricsLibrary metrics_library_;
};

// Use our "real" implementation as the Metrics object.
std::unique_ptr<MetricsInterface> MetricsInterface::GetMetricsInstance() {
  return std::make_unique<Metrics>();
}
