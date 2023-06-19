// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/vmm_swap_metrics.h"

#include <cstdint>
#include <limits>
#include <optional>
#include <string>

#include <base/check.h>
#include <base/location.h>
#include <base/logging.h>
#include <base/memory/page_size.h>
#include <base/sequence_checker.h>
#include <base/strings/strcat.h>
#include <base/time/time.h>
#include <base/timer/timer.h>
#include <vm_applications/apps.pb.h>

#include "vm_tools/common/vm_id.h"
#include "vm_tools/concierge/crosvm_control.h"

namespace vm_tools::concierge {

namespace {
static constexpr char kMetricsPrefix[] = "Memory.VmmSwap.";
static constexpr char kMetricsState[] = ".State";
static constexpr char kMetricsInactiveBeforeEnableDuration[] =
    ".InactiveBeforeEnableDuration";
static constexpr char kMetricsActiveAfterEnableDuration[] =
    ".ActiveAfterEnableDuration";
static constexpr char kMetricsInactiveNoEnableDuration[] =
    ".InactiveNoEnableDuration";
static constexpr char kMetricsMinPagesInFile[] = ".MinPagesInFile";
static constexpr char kMetricsAvgPagesInFile[] = ".AvgPagesInFile";
static constexpr char kMetricsPageAverageDurationInFile[] =
    ".PageAverageDurationInFile";
static constexpr base::TimeDelta kHeartbeatDuration = base::Minutes(10);
static constexpr int kDurationMinHours = 1;
// Policies for vmm-swap (e.g. VmmSwapTbwPolicy, VmmSwapUsagePolicy) uses 4
// weeks of history to decide when to enable vmm-swap. It is fine that durations
// longer than 28days are cut down to 28days because the metrics intends to
// monitor the effectiveness of the policies.
static constexpr int kDurationMaxHours = 24 * 28;  // 28 days
// The last bucket has less than 4 days size which is enough granularity.
static constexpr int kDurationNumBuckets = 50;
// The heartbeat runs every 10 minutes. If the most pages lives in the file only
// less than 10 minutes, that is a signal that vmm-swap is not effective.
static constexpr int kDurationInFileMinSeconds = 10 * 60;  // 10 minutes
// Policies for vmm-swap (e.g. VmmSwapTbwPolicy, VmmSwapUsagePolicy) uses 4
// weeks of history to decide when to enable vmm-swap. If most of pages lives in
// the file more than 4 weeks, vmm-swap works well enough.
static constexpr int kDurationInFileMaxSeconds = 28 * 24 * 3600;  // 28 days
// The last bucket has less than 5 days size which is enough granularity.
static constexpr int kDurationInFileNumBuckets = 50;
// Any memory savings less than 50MiB probably be considered a failure for
// vmm-swap.
static constexpr int kPagesInFileMin = (50LL * 1024 * 1024) / 4096;
// We shrink the guest memory size just before enabling vmm-swap. The swap
// file's max size shouldn't be more than 1GiB regardless of how much memory
// the device has. If the file is larger than 2GiB, it means something is
// probably going wrong.
static constexpr int kPagesInFileMax = (2LL * 1024 * 1024 * 1024) / 4096;
// The last bucket has less than 160 MiB size which is enough granularity.
static constexpr int kPagesInFileNumBuckets = 50;

std::string GetMetricsName(VmId::Type vm_type,
                           const std::string& unprefixed_metrics_name) {
  return base::StrCat(
      {kMetricsPrefix, apps::VmType_Name(vm_type), unprefixed_metrics_name});
}
}  // namespace

VmmSwapMetrics::VmmSwapMetrics(
    VmId::Type vm_type,
    const raw_ref<MetricsLibraryInterface> metrics,
    std::unique_ptr<base::RepeatingTimer> heartbeat_timer)
    : vm_type_(vm_type),
      metrics_(metrics),
      heartbeat_timer_(std::move(heartbeat_timer)) {}

void VmmSwapMetrics::OnSwappableIdleEnabled(base::Time time) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!heartbeat_timer_->IsRunning()) {
    vmm_swap_out_metrics_.reset();
    heartbeat_timer_->Start(FROM_HERE, kHeartbeatDuration, this,
                            &VmmSwapMetrics::OnHeartbeat);
  }
  if (!swappable_idle_start_time_.has_value()) {
    swappable_idle_start_time_ = time;
  }
}

void VmmSwapMetrics::OnSwappableIdleDisabled() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (heartbeat_timer_->IsRunning()) {
    heartbeat_timer_->Stop();
  }
  swappable_idle_start_time_.reset();
}

void VmmSwapMetrics::OnVmmSwapEnabled(base::Time time) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  is_enabled_ = true;
  if (!vmm_swap_enable_time_.has_value()) {
    vmm_swap_enable_time_ = time;
  }
}

void VmmSwapMetrics::OnPreVmmSwapOut(int64_t written_pages, base::Time time) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!vmm_swap_out_metrics_.has_value()) {
    vmm_swap_out_metrics_ = VmmSwapOutMetrics{
        .last_swap_out_time = time,
        .min_pages_in_file = written_pages,
        .pages_in_file = written_pages,
        .total_pages_swapped_in = 0,
        .average_pages_in_file = static_cast<double>(written_pages),
        .page_total_duration_in_file_seconds = 0,
        .count_heartbeat = 1,
    };
  } else {
    VmmSwapOutMetrics& swap_out_metrics = vmm_swap_out_metrics_.value();
    swap_out_metrics.page_total_duration_in_file_seconds +=
        (time - swap_out_metrics.last_swap_out_time).InSecondsF() *
        static_cast<double>(swap_out_metrics.pages_in_file);
    swap_out_metrics.last_swap_out_time = time;
    swap_out_metrics.pages_in_file = written_pages;
  }
}

void VmmSwapMetrics::OnVmmSwapDisabled(base::Time time) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  is_enabled_ = false;
  // Reports ".InactiveNoEnableDuration" when vmm-swap is disabled instead of
  // when swappable-idle is disabled for codebase simplicity.
  // `OnVmmSwapDisabled()` is called every time just before
  // `OnSwappableIdleDisabled()`.
  ReportDurations(time);

  ReportPagesInFile(time);
  vmm_swap_out_metrics_.reset();

  vmm_swap_enable_time_.reset();
  if (swappable_idle_start_time_.has_value()) {
    // We may be here because the low disk policy disabled vmm-swap. In that
    // case, vmm-swap may be re-enabled again at some point in future, so reset
    // swappable_idle_start_time_ to avoid double-counting time.
    swappable_idle_start_time_ = time;
  }
}

void VmmSwapMetrics::OnDestroy(base::Time time) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ReportDurations(time);
  ReportPagesInFile(time);
}

void VmmSwapMetrics::SetFetchVmmSwapStatusFunction(FetchVmmSwapStatus func) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  fetch_vmm_swap_status_ = std::move(func);
}

void VmmSwapMetrics::OnHeartbeat() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!metrics_->SendEnumToUMA(
          GetMetricsName(vm_type_, kMetricsState),
          is_enabled_ ? State::kEnabled : State::kDisabled)) {
    LOG(ERROR) << "Failed to send vmm-swap state metrics";
  }
  if (fetch_vmm_swap_status_.is_null()) {
    return;
  }
  auto status_result = fetch_vmm_swap_status_.Run();
  if (!status_result.has_value()) {
    LOG(ERROR) << "Failed fetch vmm swap status for metrics: "
               << status_result.error();
    return;
  }
  if (status_result.value().state != SwapState::ACTIVE) {
    return;
  }

  int64_t pages_in_file =
      static_cast<int64_t>(status_result.value().metrics.swap_pages);
  if (!vmm_swap_out_metrics_.has_value()) {
    LOG(ERROR) << "Metrics heartbeat executed without "
                  "VmmSwapMetrics::OnPreVmmSwapOut()";
    return;
  }

  VmmSwapOutMetrics& swap_out_metrics = vmm_swap_out_metrics_.value();
  int64_t pages_swapped_in = swap_out_metrics.pages_in_file - pages_in_file;
  if (pages_swapped_in >= 0) {
    swap_out_metrics.page_total_duration_in_file_seconds +=
        (base::Time::Now() - swap_out_metrics.last_swap_out_time).InSecondsF() *
        static_cast<double>(pages_swapped_in);
    swap_out_metrics.total_pages_swapped_in += pages_swapped_in;
    swap_out_metrics.pages_in_file = pages_in_file;
  } else {
    LOG(WARNING)
        << "pages in file increased without VmmSwapMetrics::OnPreVmmSwapOut()";
  }

  // The pages can be swapped out to file multiple times while vmm-swap is
  // enabled because pages will gradually be faulted back into memory.
  // pages_in_file is not always smaller than or equal to min_pages_in_file.
  if (pages_in_file < swap_out_metrics.min_pages_in_file) {
    swap_out_metrics.min_pages_in_file = pages_in_file;
  }
  double pages = swap_out_metrics.average_pages_in_file;
  pages *= static_cast<double>(swap_out_metrics.count_heartbeat);
  pages += static_cast<double>(pages_in_file);
  pages /= static_cast<double>(swap_out_metrics.count_heartbeat + 1);
  swap_out_metrics.average_pages_in_file = pages;
  swap_out_metrics.count_heartbeat += 1;
}

void VmmSwapMetrics::ReportDurations(base::Time time) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (vmm_swap_enable_time_.has_value()) {
    // If vmm-swap is force-enabled, `pending_started_at_` can be empty or
    // later than enabled time.
    if (swappable_idle_start_time_.has_value() &&
        swappable_idle_start_time_.value() < vmm_swap_enable_time_.value()) {
      if (!SendDurationToUMA(kMetricsInactiveBeforeEnableDuration,
                             vmm_swap_enable_time_.value() -
                                 swappable_idle_start_time_.value())) {
        LOG(ERROR)
            << "Failed to send vmm-swap pending enabled duration metrics";
      }
      if (!SendDurationToUMA(kMetricsActiveAfterEnableDuration,
                             time - vmm_swap_enable_time_.value())) {
        LOG(ERROR) << "Failed to send vmm-swap enabled duration metrics";
      }
    }
  } else {
    if (swappable_idle_start_time_.has_value()) {
      if (!SendDurationToUMA(kMetricsInactiveNoEnableDuration,
                             time - swappable_idle_start_time_.value())) {
        LOG(ERROR)
            << "Failed to send vmm-swap pending enabled duration metrics";
      }
    }
  }
}

void VmmSwapMetrics::ReportPagesInFile(base::Time time) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!vmm_swap_out_metrics_.has_value()) {
    return;
  }
  auto swap_out_metrics = vmm_swap_out_metrics_.value();
  if (!SendPagesToUMA(kMetricsMinPagesInFile,
                      swap_out_metrics.min_pages_in_file)) {
    LOG(ERROR) << "Failed to send vmm-swap min pages in file metrics";
  }
  if (!SendPagesToUMA(
          kMetricsAvgPagesInFile,
          static_cast<int64_t>(swap_out_metrics.average_pages_in_file))) {
    LOG(ERROR) << "Failed to send vmm-swap avg pages in file metrics";
  }
  int64_t total_pages =
      swap_out_metrics.pages_in_file + swap_out_metrics.total_pages_swapped_in;
  double average_seconds = 0;
  if (total_pages > 0) {
    double total_seconds =
        swap_out_metrics.page_total_duration_in_file_seconds +
        static_cast<double>(
            (time - swap_out_metrics.last_swap_out_time).InSeconds() *
            swap_out_metrics.pages_in_file);
    average_seconds = total_seconds / static_cast<double>(total_pages);
  }
  if (average_seconds < 0) {
    LOG(ERROR) << "duration in file for UMA is negative";
  } else if (!metrics_->SendToUMA(
                 GetMetricsName(vm_type_, kMetricsPageAverageDurationInFile),
                 static_cast<int>(average_seconds), kDurationInFileMinSeconds,
                 kDurationInFileMaxSeconds, kDurationInFileNumBuckets)) {
    LOG(ERROR) << "Failed to send vmm-swap avg duration pages in file metrics";
  }
}

bool VmmSwapMetrics::SendPagesToUMA(const std::string& unprefixed_metrics_name,
                                    int64_t pages) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  int pages_4KiB = static_cast<int>((pages * base::GetPageSize()) / 4096);
  return metrics_->SendToUMA(GetMetricsName(vm_type_, unprefixed_metrics_name),
                             pages_4KiB, kPagesInFileMin, kPagesInFileMax,
                             kPagesInFileNumBuckets);
}

bool VmmSwapMetrics::SendDurationToUMA(
    const std::string& unprefixed_metrics_name,
    base::TimeDelta duration) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (duration.is_negative()) {
    LOG(ERROR) << "duration for UMA is negative";
    // UMA does not support negative value for histogram.
    return false;
  }
  int hours = static_cast<int>(duration.IntDiv(base::Hours(1)));
  return metrics_->SendToUMA(GetMetricsName(vm_type_, unprefixed_metrics_name),
                             hours, kDurationMinHours, kDurationMaxHours,
                             kDurationNumBuckets);
}

}  // namespace vm_tools::concierge
