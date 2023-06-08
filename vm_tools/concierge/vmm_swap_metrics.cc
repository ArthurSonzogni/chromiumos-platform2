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
static constexpr base::TimeDelta kHeartbeatDuration = base::Minutes(10);

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

void VmmSwapMetrics::OnSwappableIdleEnabled() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!heartbeat_timer_->IsRunning()) {
    heartbeat_timer_->Start(FROM_HERE, kHeartbeatDuration, this,
                            &VmmSwapMetrics::OnHeartbeat);
  }
}

void VmmSwapMetrics::OnSwappableIdleDisabled() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (heartbeat_timer_->IsRunning()) {
    heartbeat_timer_->Stop();
  }
}

void VmmSwapMetrics::OnVmmSwapEnabled() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  is_enabled_ = true;
}

void VmmSwapMetrics::OnVmmSwapDisabled() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  is_enabled_ = false;
}

void VmmSwapMetrics::OnHeartbeat() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!metrics_->SendEnumToUMA(
          GetMetricsName(vm_type_, kMetricsState),
          is_enabled_ ? State::kEnabled : State::kDisabled)) {
    LOG(ERROR) << "Failed to send vmm-swap state metrics";
  }
}

}  // namespace vm_tools::concierge
