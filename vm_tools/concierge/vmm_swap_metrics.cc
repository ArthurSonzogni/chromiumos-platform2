// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/vmm_swap_metrics.h"

#include <string>

#include <base/location.h>
#include <base/logging.h>
#include <base/sequence_checker.h>
#include <base/strings/strcat.h>
#include <base/time/time.h>
#include <base/timer/timer.h>
#include <vm_applications/apps.pb.h>

#include "vm_tools/common/vm_id.h"

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
static constexpr base::TimeDelta kHeartbeatDuration = base::Minutes(10);
static constexpr int kDurationMinHours = 1;
// Policies for vmm-swap (e.g. VmmSwapTbwPolicy, VmmSwapUsagePolicy) uses 4
// weeks of history to decide when to enable vmm-swap. It is fine that durations
// longer than 28days are cut down to 28days because the metrics intends to
// monitor the effectiveness of the policies.
static constexpr int kDurationMaxHours = 24 * 28;  // 28 days
// The last bucket has less than 4 days size which is enough granularity.
static constexpr int kDurationNumBuckets = 50;

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

void VmmSwapMetrics::OnVmmSwapDisabled(base::Time time) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  is_enabled_ = false;
  // Reports ".InactiveNoEnableDuration" when vmm-swap is disabled instead of
  // when swappable-idle is disabled for codebase simplicity.
  // `OnVmmSwapDisabled()` is called every time just before
  // `OnSwappableIdleDisabled()`.
  ReportDurations(time);

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
}

void VmmSwapMetrics::OnHeartbeat() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!metrics_->SendEnumToUMA(
          GetMetricsName(vm_type_, kMetricsState),
          is_enabled_ ? State::kEnabled : State::kDisabled)) {
    LOG(ERROR) << "Failed to send vmm-swap state metrics";
  }
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
