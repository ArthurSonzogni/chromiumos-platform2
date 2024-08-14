// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "swap_management/metrics.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

#include <absl/status/status.h>
#include <base/metrics/histogram_functions.h>
#include <base/metrics/histogram_macros.h>
#include <base/strings/string_split.h>

#include "swap_management/utils.h"
#include "swap_management/zram_stats.h"

namespace swap_management {

namespace {
constexpr uint32_t kNumAbslStatus = 21;

// Default period for reporting psi and zram metrics. Must be either 10, 60 or
// 300 to match psi report.
constexpr uint32_t kDefaultPeriodSec = 10;
// Max number of pages should be the max system memory divided by the smallest
// possible page size, or: 32GB / 4096
constexpr uint64_t kMaxNumPages = (static_cast<uint64_t>(32) << 30) / (4096);
}  // namespace

Metrics* Metrics::Get() {
  return *GetSingleton<Metrics>();
}

Metrics::~Metrics() {
  Stop();
}

void Metrics::OverrideForTesting(Metrics* metrics) {
  [[maybe_unused]] static bool is_overridden = []() -> bool {
    if (*GetSingleton<Metrics>())
      delete *GetSingleton<Metrics>();
    return true;
  }();
  *GetSingleton<Metrics>() = metrics;
}

void Metrics::ReportSwapStartStatus(absl::Status status) {
  metrics_.SendEnumToUMA("ChromeOS.SwapManagement.SwapStart.Status",
                         static_cast<int>(status.code()), kNumAbslStatus);
}

void Metrics::ReportSwapStopStatus(absl::Status status) {
  metrics_.SendEnumToUMA("ChromeOS.SwapManagement.SwapStop.Status",
                         static_cast<int>(status.code()), kNumAbslStatus);
}

void Metrics::PeriodicReportZramMetrics() {
  absl::StatusOr<ZramMmStat> zram_mm_stat = GetZramMmStat();
  LOG_IF(ERROR, !zram_mm_stat.ok())
      << "Failed to read zram mm stat: " << zram_mm_stat.status();

  if (zram_mm_stat.ok()) {
    const uint64_t kTotalPagesSwapped =
        (*zram_mm_stat).orig_data_size / kPageSize;

    metrics_.SendToUMA("ChromeOS.Zram.OrigDataSizeMB",
                       (*zram_mm_stat).orig_data_size / kMiB, 1, 64000, 100);

    metrics_.SendToUMA("ChromeOS.Zram.ComprDataSizeMB",
                       (*zram_mm_stat).compr_data_size / kMiB, 1, 64000, 100);

    metrics_.SendPercentageToUMA("ChromeOS.Zram.CompressedSizePct",
                                 (*zram_mm_stat).orig_data_size
                                     ? ((*zram_mm_stat).compr_data_size *
                                        100.0 / (*zram_mm_stat).orig_data_size)
                                     : 0);

    metrics_.SendToUMA("ChromeOS.Zram.MemUsedTotalMB",
                       (*zram_mm_stat).mem_used_total / kMiB, 1, 64000, 100);

    metrics_.SendToUMA("ChromeOS.Zram.MemLimitMB",
                       (*zram_mm_stat).mem_limit / kMiB, 1, 64000, 100);

    metrics_.SendToUMA("ChromeOS.Zram.MemUsedMaxMB",
                       (*zram_mm_stat).mem_used_max / kMiB, 1, 64000, 100);

    metrics_.SendToUMA("ChromeOS.Zram.SamePages", (*zram_mm_stat).same_pages, 1,
                       kMaxNumPages, 50);

    metrics_.SendPercentageToUMA(
        "ChromeOS.Zram.SamePagesPct",
        kTotalPagesSwapped
            ? (*zram_mm_stat).same_pages * 100.0 / kTotalPagesSwapped
            : 0);

    metrics_.SendToUMA("ChromeOS.Zram.PagesCompacted",
                       (*zram_mm_stat).pages_compacted, 1, kMaxNumPages, 50);

    if ((*zram_mm_stat).huge_pages) {
      metrics_.SendToUMA("ChromeOS.Zram.HugePages", *(*zram_mm_stat).huge_pages,
                         1, kMaxNumPages, 50);

      metrics_.SendPercentageToUMA(
          "ChromeOS.Zram.HugePagesPct",
          kTotalPagesSwapped
              ? *(*zram_mm_stat).huge_pages * 100.0 / kTotalPagesSwapped
              : 0);

      if ((*zram_mm_stat).huge_pages_since) {
        metrics_.SendToUMA("ChromeOS.Zram.HugePagesSince",
                           *(*zram_mm_stat).huge_pages_since, 1, kMaxNumPages,
                           50);

        if (has_old_huge_pages_) {
          int64_t stored =
              *(*zram_mm_stat).huge_pages_since - old_huge_pages_since_;
          // The delta in 'stored' minus the growth in state is the number of
          // pages removed.
          int64_t removed =
              stored - (*(*zram_mm_stat).huge_pages - old_huge_pages_);
          if (stored >= 0 && removed >= 0) {
            metrics_.SendToUMA("ChromeOS.Zram.HugePagesStored", stored, 1,
                               kMaxNumPages, 50);
            metrics_.SendToUMA("ChromeOS.Zram.HugePagesRemoved", removed, 1,
                               kMaxNumPages, 50);
          }
        }
        // Save for next time.
        has_old_huge_pages_ = true;
        old_huge_pages_ = *(*zram_mm_stat).huge_pages;
        old_huge_pages_since_ = *(*zram_mm_stat).huge_pages_since;
      }
    }
  }

  absl::StatusOr<ZramBdStat> zram_bd_stat = GetZramBdStat();
  LOG_IF(ERROR, !zram_bd_stat.ok())
      << "Failed to read zram bd stat: " << zram_bd_stat.status();

  if (zram_bd_stat.ok()) {
    metrics_.SendToUMA("ChromeOS.Zram.BdCount", (*zram_bd_stat).bd_count, 1,
                       1000000, 50);

    metrics_.SendToUMA("ChromeOS.Zram.BdReads", (*zram_bd_stat).bd_reads, 1,
                       1000000, 50);

    metrics_.SendToUMA("ChromeOS.Zram.BdWrites", (*zram_bd_stat).bd_writes, 1,
                       1000000, 50);
  }

  absl::StatusOr<ZramIoStat> zram_io_stat = GetZramIoStat();
  LOG_IF(ERROR, !zram_io_stat.ok())
      << "Failed to read zram io stat: " << zram_io_stat.status();

  metrics_.SendToUMA("ChromeOS.Zram.FailedReads", (*zram_io_stat).failed_reads,
                     1, 1000, 50);

  metrics_.SendToUMA("ChromeOS.Zram.FailedWrites",
                     (*zram_io_stat).failed_writes, 1, 1000, 50);

  metrics_.SendToUMA("ChromeOS.Zram.InvalidIo", (*zram_io_stat).invalid_io, 1,
                     1000, 50);

  metrics_.SendToUMA("ChromeOS.Zram.NotifyFree", (*zram_io_stat).notify_free, 1,
                     1000, 50);

  // Values as logged in the histogram for (Memory|CPU|IO) pressure.
  constexpr uint32_t kPressureMin = 1;  // As 0 is for underflow.
  constexpr uint32_t kPressureExclusiveMax = 10000;
  constexpr uint32_t kPressureHistogramBuckets = 100;

  absl::StatusOr<std::vector<uint32_t>> psi_memory_metrics =
      PSIParser(base::FilePath("/proc/pressure/memory"), kDefaultPeriodSec);
  LOG_IF(ERROR, !psi_memory_metrics.ok())
      << "Failed to read PSI memory metrics: " << psi_memory_metrics.status();
  if (psi_memory_metrics.ok()) {
    metrics_.SendToUMA("ChromeOS.CWP.PSIMemPressure.Some",
                       (*psi_memory_metrics)[0], kPressureMin,
                       kPressureExclusiveMax, kPressureHistogramBuckets);
    metrics_.SendToUMA("ChromeOS.CWP.PSIMemPressure.Full",
                       (*psi_memory_metrics)[1], kPressureMin,
                       kPressureExclusiveMax, kPressureHistogramBuckets);
  }

  absl::StatusOr<std::vector<uint32_t>> psi_cpu_metrics =
      PSIParser(base::FilePath("/proc/pressure/cpu"), kDefaultPeriodSec);
  LOG_IF(ERROR, !psi_cpu_metrics.ok())
      << "Failed to read PSI cpu metrics: " << psi_cpu_metrics.status();
  if (psi_cpu_metrics.ok()) {
    metrics_.SendToUMA("ChromeOS.CWP.PSICpuPressure.Some",
                       (*psi_cpu_metrics)[0], kPressureMin,
                       kPressureExclusiveMax, kPressureHistogramBuckets);
    metrics_.SendToUMA("ChromeOS.CWP.PSICpuPressure.Full",
                       (*psi_cpu_metrics)[1], kPressureMin,
                       kPressureExclusiveMax, kPressureHistogramBuckets);
  }

  absl::StatusOr<std::vector<uint32_t>> psi_io_metrics =
      PSIParser(base::FilePath("/proc/pressure/io"), kDefaultPeriodSec);
  LOG_IF(ERROR, !psi_io_metrics.ok())
      << "Failed to read PSI io metrics: " << psi_io_metrics.status();
  if (psi_io_metrics.ok()) {
    metrics_.SendToUMA("ChromeOS.CWP.PSIIoPressure.Some", (*psi_io_metrics)[0],
                       kPressureMin, kPressureExclusiveMax,
                       kPressureHistogramBuckets);
    metrics_.SendToUMA("ChromeOS.CWP.PSIIoPressure.Full", (*psi_io_metrics)[1],
                       kPressureMin, kPressureExclusiveMax,
                       kPressureHistogramBuckets);
  }

  // We use exactly 15 buckets for zram, each of size 1GB except for the last
  // which is unbounded. This means we have buckets: [0, 1), [1, 2), ..., [14,
  // infinity).
  constexpr uint32_t kZramBucketCount = 15;
  uint32_t zram_bucket = (*zram_mm_stat).orig_data_size / kMiB / 1024;
  zram_bucket = std::min(kZramBucketCount - 1, zram_bucket);

  // We use exactly 20 buckets for metric_some of width 5 between 0 and 100.
  constexpr uint32_t kPsiBucketWidth = 5;
  constexpr uint32_t kPsiBucketCount = 100 / kPsiBucketWidth;
  uint32_t psi_bucket = (*psi_memory_metrics)[0] / kPsiBucketWidth;
  psi_bucket = std::min(kPsiBucketCount - 1, psi_bucket);

  uint32_t composite_bucket = zram_bucket * kPsiBucketCount + psi_bucket;

  metrics_.SendEnumToUMA("ChromeOS.Zram.PSISomeOrigDataSizeMB",
                         composite_bucket, kZramBucketCount * kPsiBucketCount);
}

void Metrics::Start() {
  // Start periodic writeback.
  metrics_timer_.Start(FROM_HERE, base::Seconds(kDefaultPeriodSec),
                       base::BindRepeating(&Metrics::PeriodicReportZramMetrics,
                                           weak_factory_.GetWeakPtr()));
}

void Metrics::Stop() {
  metrics_timer_.Stop();
}

absl::StatusOr<std::vector<uint32_t>> Metrics::PSIParser(base::FilePath path,
                                                         uint32_t period) {
  if (period != 10 && period != 60 && period != 300)
    return absl::InvalidArgumentError("Invalid PSI period " +
                                      std::to_string(period));

  std::string content;
  absl::Status status = Utils::Get()->ReadFileToString(path, &content);
  if (!status.ok())
    return status;

  std::vector<std::string> tokens = base::SplitString(
      content, " \n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);

  // Example output for /proc/pressure/memory:
  // some avg10=0.10 avg60=3.85 avg300=2.01 total=7693280
  // full avg10=0.10 avg60=3.85 avg300=2.01 total=7689487
  // After split with space and newline, there will be two tokens start with
  // prefix "avg|period|=", the first is some psi, and the next is full psi, in
  // |period|.
  std::vector<uint32_t> res;
  std::string metric_prefix = "avg" + std::to_string(period) + "=";

  for (const std::string& t : tokens) {
    if (t.find(metric_prefix) != std::string::npos) {
      std::string metric_in_text =
          t.substr(t.find(metric_prefix) + metric_prefix.length());
      absl::StatusOr<double> metric = Utils::Get()->SimpleAtod(metric_in_text);
      if (!metric.ok())
        return metric.status();

      // Want to multiply by 100, but to avoid integer truncation,
      // do best-effort rounding.
      const uint32_t preround = static_cast<uint32_t>(*metric * 1000);
      res.push_back((preround + 5) / 10);
    }
  }

  // Sanity check if res contains two entries.
  if (res.size() != 2)
    return absl::InternalError("Failed to parse PSI metrics.");

  return res;
}

void Metrics::EnableZramWritebackMetrics() {
  last_zram_bd_stat_ = std::make_unique<ZramBdStat>();

  // Report writeback metrics every 24hr.
  writeback_metrics_timer_.Start(
      FROM_HERE, base::Days(1),
      base::BindRepeating(&Metrics::PeriodicReportZramWritebackMetrics,
                          weak_factory_.GetWeakPtr()));
}

void Metrics::PeriodicReportZramWritebackMetrics() {
  absl::StatusOr<ZramBdStat> zram_bd_stat = GetZramBdStat();
  LOG_IF(ERROR, !zram_bd_stat.ok())
      << "Failed to read zram bd stat: " << zram_bd_stat.status();

  uint64_t bd_write_delta =
      (*zram_bd_stat).bd_writes - last_zram_bd_stat_->bd_writes;

  metrics_.SendToUMA("ChromeOS.Zram.WritebackPagesPerDay", bd_write_delta, 0,
                     (4ul << 30) / kPageSize, 100);

  last_zram_bd_stat_ = std::make_unique<ZramBdStat>(std::move(*zram_bd_stat));
}

}  // namespace swap_management
