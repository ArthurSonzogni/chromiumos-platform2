// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/mm/balloon_metrics.h"

#include <limits>
#include "base/time/time.h"

#include <base/logging.h>
#include <base/strings/strcat.h>

namespace vm_tools::concierge::mm {

namespace {

// The prefix for all Virtual Machine Memory Management Service metrics.
constexpr char kMetricsPrefix[] = "Memory.VMMMS.";

// Deflate tracks the size of balloon deflations. We will use this metric to
// compare balloon size changes between VMMMS and the LimitCacheBalloonPolicy,
// and to detect frequent large balloon resizes.
constexpr char kDeflateMetric[] = ".Deflate";
constexpr int kDeflateMetricMinMiB = 0;
constexpr int kDeflateMetricMaxMiB = 3200;
constexpr int kDeflateMetricBuckets = 100;

// Inflate tracks the size of balloon inflations. Used the same way as Deflate.
constexpr char kInflateMetric[] = ".Inflate";
constexpr int kInflateMetricMinMiB = kDeflateMetricMinMiB;
constexpr int kInflateMetricMaxMiB = kDeflateMetricMaxMiB;
constexpr int kInflateMetricBuckets = kDeflateMetricBuckets;

// ResizeInterval tracks the time between balloon resizes. We will use this
// metric to compare the frequency of balloon sizes between VMMMS and the
// LimitCacheBalloonPolicy, and to detect balloon thrashing.
constexpr char kResizeIntervalMetric[] = ".ResizeInterval";
constexpr base::TimeDelta kResizeIntervalMetricMinTimeDelta = base::Seconds(0);
constexpr base::TimeDelta kResizeIntervalMetricMaxTimeDelta =
    base::Seconds(1000);
constexpr size_t kResizeIntervalMetricBuckets = 100;

// Size tracks the size of the balloon over time. This metric is logged on
// balloon resize, but not more than once per 10 minutes. We will use this
// metric to compare VMMMS with the LimitCacheBalloonPolicy, and to evaluate
// ARCVM memory efficiency.
constexpr char kSizeMetric[] = ".Size10Minutes";
constexpr int kSizeMetricMinMiB = 0;
// The maximum size of a VM is 15GiB on a 16GiB board. With 100 buckets, that
// gives us a granularity of 154 MiB, which should be good enough.
constexpr int kSizeMetricMaxMiB = 15360;
constexpr int kSizeMetricBuckets = 100;
constexpr base::TimeDelta kSizeMetricInterval = base::Minutes(10);

// StallThroughput tracks the speed of the balloon just before a stall. We will
// use this metric to tune the stall detection threshold.
constexpr char kStallThroughputMetric[] = ".StallThroughput";
constexpr int kStallThroughputMetricMaxMiBps = 60;

std::string GetMetricName(apps::VmType vm_type,
                          const std::string& unprefixed_metric_name) {
  return base::StrCat(
      {kMetricsPrefix, apps::VmType_Name(vm_type), unprefixed_metric_name});
}

}  // namespace

BalloonMetrics::BalloonMetrics(
    apps::VmType vm_type,
    const raw_ref<MetricsLibraryInterface> metrics,
    base::RepeatingCallback<base::TimeTicks(void)> time_ticks_now)
    : vm_type_(vm_type),
      metrics_(metrics),
      time_ticks_now_(time_ticks_now),
      resize_interval_start_(time_ticks_now.Run()),
      last_size_log_time_(resize_interval_start_),
      last_size_mib_(0) {}

BalloonMetrics::~BalloonMetrics() {
  LogSizeIfNeeded(last_size_mib_, time_ticks_now_.Run());
}

void BalloonMetrics::OnResize(Balloon::ResizeResult result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  const base::TimeTicks now = time_ticks_now_.Run();

  const base::TimeDelta resize_interval = now - resize_interval_start_;
  resize_interval_start_ = now;
  metrics_->SendTimeToUMA(GetMetricName(vm_type_, kResizeIntervalMetric),
                          resize_interval, kResizeIntervalMetricMinTimeDelta,
                          kResizeIntervalMetricMaxTimeDelta,
                          kResizeIntervalMetricBuckets);

  const int abs_delta_mib = std::abs(result.actual_delta_bytes / MiB(1));
  if (result.actual_delta_bytes > 0) {
    metrics_->SendToUMA(GetMetricName(vm_type_, kInflateMetric), abs_delta_mib,
                        kInflateMetricMinMiB, kInflateMetricMaxMiB,
                        kInflateMetricBuckets);
  } else {
    metrics_->SendToUMA(GetMetricName(vm_type_, kDeflateMetric), abs_delta_mib,
                        kDeflateMetricMinMiB, kDeflateMetricMaxMiB,
                        kDeflateMetricBuckets);
  }

  // We are logging the past balloon size, so we need the size before the most
  // recent resize, so subtract the delta.
  const int size_mib = static_cast<int>(
      (result.new_target - result.actual_delta_bytes) / MiB(1));
  LogSizeIfNeeded(size_mib, now);
  last_size_mib_ = static_cast<int>(result.new_target / MiB(1));
}

void BalloonMetrics::OnStall(Balloon::StallStatistics stats) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  metrics_->SendLinearToUMA(GetMetricName(vm_type_, kStallThroughputMetric),
                            stats.inflate_mb_per_s,
                            kStallThroughputMetricMaxMiBps);
}

apps::VmType BalloonMetrics::GetVmType() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  return vm_type_;
}

void BalloonMetrics::LogSizeIfNeeded(int size_mib, base::TimeTicks now) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  const int64_t size_samples =
      (now - last_size_log_time_).IntDiv(kSizeMetricInterval);
  if (size_samples < 0 || size_samples > std::numeric_limits<int>::max()) {
    LOG(WARNING) << "Balloon size sample count is out of bounds: "
                 << size_samples;
  } else if (size_samples > 0) {
    metrics_->SendRepeatedToUMA(GetMetricName(vm_type_, kSizeMetric), size_mib,
                                kSizeMetricMinMiB, kSizeMetricMaxMiB,
                                kSizeMetricBuckets, size_samples);
    last_size_log_time_ += kSizeMetricInterval * size_samples;
  }
}

}  // namespace vm_tools::concierge::mm
