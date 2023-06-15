// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_VMM_SWAP_METRICS_H_
#define VM_TOOLS_CONCIERGE_VMM_SWAP_METRICS_H_

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <base/time/time.h>
#include <base/timer/timer.h>
#include <base/sequence_checker.h>
#include <metrics/metrics_library.h>

#include "vm_tools/common/vm_id.h"

namespace vm_tools::concierge {

// Logs UMA metrics for vmm-swap feature.
//
// * "Memory.VmmSwap.<vm name>.State"
//
// Sends the vmm-swap state (enabled or disabled) every 10 minutes while the vm
// is swappable-idle. Vmm-swap may not be enabled by policies to protect disk.
// This metrics indicates what percentage of time in the swappable-idle state is
// spent with vmm-swap enabled and how well the policies works.
//
// * "Memory.VmmSwap.<vm name>.InactiveBeforeEnableDuration"
//
// The duration how long it was spent waiting to enable vmm-swap since it
// becomes swappable-idle. This is sent with ".ActiveAfterEnableDuration"
// metrics when vmm-swap is disabled.
//
// * "Memory.VmmSwap.<vm name>.ActiveAfterEnableDuration"
//
// The duration how long it was spent with vmm-swap enabled. This is sent with
// ".InactiveBeforeEnableDuration" metrics when vmm-swap is disabled.
// Shorter ".InactiveBeforeEnableDuration" and longer
// ".ActiveAfterEnableDuration" indicates the vmm-swap policies are doing a good
// job at deciding when to enable vmm-swap.
//
// * "Memory.VmmSwap.<vm name>.InactiveNoEnableDuration"
//
// The duration how long it was spend without vmm-swap enabled and it exits
// swappable-idle.
// If reported values are mostly long, it indicates that the policies are
// missing chances to enable vmm-swap.
class VmmSwapMetrics final {
 public:
  VmmSwapMetrics(VmId::Type vm_type,
                 const raw_ref<MetricsLibraryInterface> metrics,
                 std::unique_ptr<base::RepeatingTimer> heartbeat_timer =
                     std::make_unique<base::RepeatingTimer>());
  VmmSwapMetrics(const VmmSwapMetrics&) = delete;
  VmmSwapMetrics& operator=(const VmmSwapMetrics&) = delete;
  ~VmmSwapMetrics() = default;

  // Enum showing whether the vmm-swap is enabled or not while it is
  // swappable-idle. This enum is used in UMA and defined at
  // `tools/metrics/histograms/enums.xml` in Chromium as `VmmSwapState`, and
  // cannot be modified independently.
  enum class State : int {
    kEnabled = 0,
    kDisabled = 1,
    kMaxValue = kDisabled,
  };

  // When SwapVm DBus method tries to enable vmm-swap. This means the vm is idle
  // and ready to enable vmm-swap.
  void OnSwappableIdleEnabled(base::Time time = base::Time::Now());
  // When SwapVm DBus method tries to disable vmm-swap.
  void OnSwappableIdleDisabled();

  // When vmm-swap is enabled.
  void OnVmmSwapEnabled(base::Time time = base::Time::Now());
  // When vmm-swap is disabled. Vmm-swap can be disabled not only by SwapVm DBus
  // method but also by low disk signals.
  void OnVmmSwapDisabled(base::Time time = base::Time::Now());
  // When ArcVm shutdown.
  void OnDestroy(base::Time time = base::Time::Now());

 private:
  void OnHeartbeat();
  void ReportDurations(base::Time time) const;
  bool SendDurationToUMA(const std::string& unprefixed_metrics_name,
                         base::TimeDelta duration) const;

  const VmId::Type vm_type_;
  const raw_ref<MetricsLibraryInterface> metrics_
      GUARDED_BY_CONTEXT(sequence_checker_);
  std::unique_ptr<base::RepeatingTimer> heartbeat_timer_
      GUARDED_BY_CONTEXT(sequence_checker_);
  bool is_enabled_ GUARDED_BY_CONTEXT(sequence_checker_) = false;
  std::optional<base::Time> swappable_idle_start_time_
      GUARDED_BY_CONTEXT(sequence_checker_);
  std::optional<base::Time> vmm_swap_enable_time_
      GUARDED_BY_CONTEXT(sequence_checker_);

  // Ensure calls are made on the right thread.
  SEQUENCE_CHECKER(sequence_checker_);
};

}  // namespace vm_tools::concierge

#endif  // VM_TOOLS_CONCIERGE_VMM_SWAP_METRICS_H_
