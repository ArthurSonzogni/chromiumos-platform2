// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/periodic_metrics.h"

#include <string>
#include <vector>

#include <base/check_op.h>
#include <base/files/file_path.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/system/sys_info.h>
#include <base/time/time.h>

namespace odml {

namespace {

// UMA metric names:
constexpr char kCpuUsageMetricName[] = "Platform.Odml.CpuUsageMilliPercent";
constexpr char kTotalRssMemoryMetricName[] = "Platform.Odml.TotalRssMemoryKb";
constexpr char kPeakTotalRssMemoryMetricName[] =
    "Platform.Odml.PeakTotalRssMemoryKb";
constexpr char kTotalSwapMemoryMetricName[] = "Platform.Odml.TotalSwapMemoryKb";
constexpr char kPeakTotalSwapMemoryMetricName[] =
    "Platform.Odml.PeakTotalSwapMemoryKb";
constexpr char kTotalMallocMemoryMetricName[] =
    "Platform.Odml.TotalMallocMemoryKb";
constexpr char kPeakTotalMallocMemoryMetricName[] =
    "Platform.Odml.PeakTotalMallocMemoryKb";

// UMA histogram ranges:
constexpr int kCpuUsagePercentScale = 1000;                            // 100%
constexpr int kCpuUsageMinMilliPercent = 1;                            // 0.001%
constexpr int kCpuUsageMaxMilliPercent = 100 * kCpuUsagePercentScale;  // 100%
constexpr int kCpuUsageBuckets = 25;
constexpr int kMemoryUsageMinKb = 10;         // 10 KB
constexpr int kMemoryUsageMaxKb = 100000000;  // 100 GB
constexpr int kMemoryUsageBuckets = 100;
constexpr int kMemoryKbScale = 1024;  // 1KB

// chromeos_metrics::CumulativeMetrics constants:
constexpr char kCumulativeMetricsBackingDir[] = "/var/lib/odml/metrics";
constexpr char kPeakTotalRssCumulativeStatName[] = "peak_rss_kb";
constexpr char kPeakTotalSwapCumulativeStatName[] = "peak_swap_kb";
constexpr char kPeakTotalMallocCumulativeStatName[] = "peak_malloc_kb";

constexpr base::TimeDelta kCumulativeMetricsUpdatePeriod = base::Minutes(5);
constexpr base::TimeDelta kCumulativeMetricsReportPeriod = base::Hours(1);

}  // namespace

PeriodicMetrics::PeriodicMetrics(raw_ref<MetricsLibrary> metrics)
    : metrics_(metrics),
      process_metrics_(base::ProcessMetrics::CreateCurrentProcessMetrics()) {
  cumulative_metrics_ = std::make_unique<chromeos_metrics::CumulativeMetrics>(
      base::FilePath(kCumulativeMetricsBackingDir),
      std::vector<std::string>({kPeakTotalRssCumulativeStatName,
                                kPeakTotalSwapCumulativeStatName,
                                kPeakTotalMallocCumulativeStatName}),
      kCumulativeMetricsUpdatePeriod,
      base::BindRepeating(&PeriodicMetrics::UpdateAndRecordMetrics,
                          weak_factory_.GetWeakPtr()),
      kCumulativeMetricsReportPeriod,
      base::BindRepeating(&PeriodicMetrics::UploadMetrics,
                          weak_factory_.GetWeakPtr()));
}

void PeriodicMetrics::UpdateAndRecordMetricsNow() {
  UpdateAndRecordMetrics(cumulative_metrics_.get());
}

void PeriodicMetrics::UpdateAndRecordMetrics(
    chromeos_metrics::CumulativeMetrics* const cumulative_metrics) {
  auto info = process_metrics_->GetMemoryInfo();
  size_t resident_set_size = info.has_value() ? info->resident_set_bytes : 0;
  size_t swap_size = info.has_value() ? info->vm_swap_bytes : 0;
  size_t malloc_size = process_metrics_->GetMallocUsage();

  // Update max memory stats.
  cumulative_metrics->Max(kPeakTotalRssCumulativeStatName,
                          static_cast<int64_t>(resident_set_size));
  cumulative_metrics->Max(kPeakTotalSwapCumulativeStatName,
                          static_cast<int64_t>(swap_size));
  cumulative_metrics->Max(kPeakTotalMallocCumulativeStatName,
                          static_cast<int64_t>(malloc_size));
}

void PeriodicMetrics::UploadMetrics(
    chromeos_metrics::CumulativeMetrics* const cumulative_metrics) {
  // Report the peak memory usage in the past.
  metrics_->SendToUMA(
      kPeakTotalRssMemoryMetricName,
      cumulative_metrics->GetAndClear(kPeakTotalRssCumulativeStatName) /
          kMemoryKbScale,
      kMemoryUsageMinKb, kMemoryUsageMaxKb, kMemoryUsageBuckets);

  metrics_->SendToUMA(
      kPeakTotalSwapMemoryMetricName,
      cumulative_metrics->GetAndClear(kPeakTotalSwapCumulativeStatName) /
          kMemoryKbScale,
      kMemoryUsageMinKb, kMemoryUsageMaxKb, kMemoryUsageBuckets);

  metrics_->SendToUMA(
      kPeakTotalMallocMemoryMetricName,
      cumulative_metrics->GetAndClear(kPeakTotalMallocCumulativeStatName) /
          kMemoryKbScale,
      kMemoryUsageMinKb, kMemoryUsageMaxKb, kMemoryUsageBuckets);

  // Report the current memory usage.
  auto info = process_metrics_->GetMemoryInfo();
  size_t resident_set_size = info.has_value() ? info->resident_set_bytes : 0;
  size_t swap_size = info.has_value() ? info->vm_swap_bytes : 0;
  size_t malloc_size = process_metrics_->GetMallocUsage();

  metrics_->SendToUMA(kTotalRssMemoryMetricName,
                      resident_set_size / kMemoryKbScale, kMemoryUsageMinKb,
                      kMemoryUsageMaxKb, kMemoryUsageBuckets);

  metrics_->SendToUMA(kTotalSwapMemoryMetricName, swap_size / kMemoryKbScale,
                      kMemoryUsageMinKb, kMemoryUsageMaxKb,
                      kMemoryUsageBuckets);

  metrics_->SendToUMA(kTotalMallocMemoryMetricName,
                      malloc_size / kMemoryKbScale, kMemoryUsageMinKb,
                      kMemoryUsageMaxKb, kMemoryUsageBuckets);

  // Record CPU usage (units = milli-percent i.e. 0.001%):
  // First get the CPU usage of the control process.
  auto cpu_usage =
      process_metrics_->GetPlatformIndependentCPUUsage().value_or(0);

  const int cpu_usage_milli_percent = static_cast<int>(
      kCpuUsagePercentScale * cpu_usage / base::SysInfo::NumberOfProcessors());
  metrics_->SendToUMA(kCpuUsageMetricName, cpu_usage_milli_percent,
                      kCpuUsageMinMilliPercent, kCpuUsageMaxMilliPercent,
                      kCpuUsageBuckets);
}

}  // namespace odml
