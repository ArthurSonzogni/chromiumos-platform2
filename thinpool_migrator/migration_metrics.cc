// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "thinpool_migrator/migration_metrics.h"

#include <metrics/metrics_library.h>
#include <metrics/timer.h>

namespace thinpool_migrator {
namespace {
MetricsLibraryInterface* g_metrics = nullptr;
constexpr const char kMetricsLogPath[] = "/run/thinpool_migrator/metrics";
constexpr const int kTimeMinMs = 0;
constexpr const int kTimeMaxMs = 30 * 1000;
constexpr const int kTimeBuckets = 50;

}  // namespace

void InitializeMetrics() {
  g_metrics = new MetricsLibrary();
  g_metrics->SetOutputFile(kMetricsLogPath);
  chromeos_metrics::TimerReporter::set_metrics_lib(g_metrics);
}

void TearDownMetrics() {
  if (g_metrics) {
    delete g_metrics;
    g_metrics = nullptr;
  }
  chromeos_metrics::TimerReporter::set_metrics_lib(nullptr);
}

void OverrideMetricsLibraryForTesting(MetricsLibraryInterface* lib) {
  g_metrics = lib;
}

void ClearMetricsLibraryForTesting() {
  g_metrics = nullptr;
}

void ReportIntMetric(const std::string& metric, int sample, int max) {
  if (!g_metrics)
    return;

  g_metrics->SendEnumToUMA(metric, sample, max);
}

ScopedTimerReporter::ScopedTimerReporter(const std::string& histogram_name)
    : TimerReporter(histogram_name, kTimeMinMs, kTimeMaxMs, kTimeBuckets) {
  Start();
}

ScopedTimerReporter::~ScopedTimerReporter() {
  Stop();
  ReportMilliseconds();
}

}  // namespace thinpool_migrator
