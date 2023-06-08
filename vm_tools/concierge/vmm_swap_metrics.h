// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_VMM_SWAP_METRICS_H_
#define VM_TOOLS_CONCIERGE_VMM_SWAP_METRICS_H_

#include <memory>
#include <string>
#include <utility>

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
class VmmSwapMetrics final {
 public:
  explicit VmmSwapMetrics(
      VmId::Type vm_type,
      const raw_ref<MetricsLibraryInterface> metrics,
      std::unique_ptr<base::RepeatingTimer> heartbeat_timer =
          std::make_unique<base::RepeatingTimer>());
  VmmSwapMetrics(const VmmSwapMetrics&) = delete;
  VmmSwapMetrics& operator=(const VmmSwapMetrics&) = delete;
  ~VmmSwapMetrics() = default;

  enum class State : int {
    kEnabled = 0,
    kDisabled = 1,
    kMaxValue = kDisabled,
  };

  // When SwapVm DBus method tries to enable vmm-swap. This means the vm is idle
  // and ready to enable vmm-swap.
  void OnSwappableIdleEnabled();
  // When SwapVm DBus method tries to disable vmm-swap.
  void OnSwappableIdleDisabled();

  // When vmm-swap is enabled.
  void OnVmmSwapEnabled();
  // When vmm-swap is disabled. Vmm-swap can be disabled not only by SwapVm DBus
  // method but also by low disk signals.
  void OnVmmSwapDisabled();

 private:
  void OnHeartbeat();

  const VmId::Type vm_type_;
  const raw_ref<MetricsLibraryInterface> metrics_
      GUARDED_BY_CONTEXT(sequence_checker_);
  std::unique_ptr<base::RepeatingTimer> heartbeat_timer_
      GUARDED_BY_CONTEXT(sequence_checker_);
  bool is_enabled_ GUARDED_BY_CONTEXT(sequence_checker_) = false;

  // Ensure calls are made on the right thread.
  SEQUENCE_CHECKER(sequence_checker_);
};

}  // namespace vm_tools::concierge

#endif  // VM_TOOLS_CONCIERGE_VMM_SWAP_METRICS_H_
