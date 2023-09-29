// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_VMM_SWAP_METRICS_H_
#define VM_TOOLS_CONCIERGE_VMM_SWAP_METRICS_H_

#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <base/functional/callback_forward.h>
#include <base/time/time.h>
#include <base/timer/timer.h>
#include <base/types/expected.h>
#include <base/sequence_checker.h>
#include <metrics/metrics_library.h>

#include "vm_tools/common/vm_id.h"
#include "vm_tools/concierge/crosvm_control.h"

namespace vm_tools::concierge {

// Enum describing what caused vmm-swap to be disabled.
enum class VmmSwapDisableReason {
  // Disabled due to the target VM having shut down.
  kVmShutdown,
  // Disabled due to low/critical disk space notification.
  kLowDiskSpace,
  // Disabled due to dbus request.
  kDisableRequest
};

// Enum describing swap policy decision of how to handle an enable dbus request.
enum VmmSwapPolicyResult {
  // All policies allow vmm-swap enable
  kApprove,
  // Vmm-swap moved memory to disk recently.
  kCoolDown,
  // VmmSwapUsagePolicy: vmm-swap is predicted to be disabled soon.
  kUsagePrediction,
  // VmmSwapTbwPolicy: vmm-swap have written too much pages into disk last
  // 28 days.
  kExceededTotalBytesWrittenLimit,
  // VmmSwapLowDiskPolicy: The device does not have enough disk space
  // available.
  kLowDisk,
};

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
//
// * "Memory.VmmSwap.<vm name>.MinPagesInFile"
//
// The minimum number of pages resident on disk any given point during the
// vmm-swap period. Even if the actual page size is not 4KiB, it is recalculated
// to be the number of 4KiB pages. This is sent when vmm-swap is disabled.
//
// * "Memory.VmmSwap.<vm name>.AvgPagesInFile"
//
// An lower bound estimate of the average number of pages resident on disk over
// the duration of the vmm-swap period. Even if the actual page size is not
// 4KiB, it is recalculated to be the number of 4KiB pages. This is sent when
// vmm-swap is disabled.
//
// * "Memory.VmmSwap.<vm name>.PageAverageDurationInFile"
//
// The average duration of each page of the guest memory lives in the swap file.
// We expect most cold pages live in the swap file for a long time and hot pages
// are not swapped out but kept in the memory. The durations are measured every
// 10 minutes to estimate the average duration. This is sent when vmm-swap is
// disabled.
class VmmSwapMetrics final {
 public:
  VmmSwapMetrics(apps::VmType vm_type,
                 const raw_ref<MetricsLibraryInterface> metrics,
                 std::unique_ptr<base::RepeatingTimer> heartbeat_timer =
                     std::make_unique<base::RepeatingTimer>());
  VmmSwapMetrics(const VmmSwapMetrics&) = delete;
  VmmSwapMetrics& operator=(const VmmSwapMetrics&) = delete;
  ~VmmSwapMetrics() = default;

  using FetchVmmSwapStatus =
      base::RepeatingCallback<base::expected<SwapStatus, std::string>()>;

  // Enum showing whether the vmm-swap is enabled or not while it is
  // swappable-idle. This enum is used in UMA and defined at
  // `tools/metrics/histograms/enums.xml` in Chromium as `VmmSwapState`, and
  // cannot be modified independently.
  enum class State : int {
    kEnabled = 0,
    kDisabled = 1,
    kMaxValue = kDisabled,
  };

  // Cross-product if VmmSwapDisableReason and state at exit. This enum is used
  // in UMA and defined at `tools/metrics/histograms/enums.xml` in Chromium as
  // `VmmSwapDisableReason`, and cannot be modified independently.
  enum class DisableReasonMetric : int {
    kVmShutdownActive = 0,
    kVmShutdownInactive = 1,
    kLowDiskSpaceActive = 2,
    kLowDiskSpaceInactive = 3,
    kDisableRequestActive = 4,
    kDisableRequestInactive = 5,
    kMaxValue = kDisableRequestInactive,
  };

  // Cross-product if PolicyResult and whether swap is being enabled or
  // maintained. This enum is used in UMA and defined at
  // `tools/metrics/histograms/enums.xml` in Chromium as `VmmSwapPolicyResult`,
  // and cannot be modified independently.
  enum class PolicyResultMetric : int {
    kApproveEnable = 0,
    kCoolDownEnable = 1,
    kUsagePredictionEnable = 2,
    kExceededTotalBytesWrittenLimitEnable = 3,
    kLowDiskEnable = 4,
    kApproveMaintenance = 5,
    kCoolDownMaintenance = 6,
    kUsagePredictionMaintenance = 7,
    kExceededTotalBytesWrittenLimitMaintenance = 8,
    kLowDiskMaintenance = 9,
    kMaxValue = kLowDiskMaintenance,
  };

  // Reports a swap policy result. `is_enable_request` is true in response
  // to a request to enable vmm-swap, and false in response to a request to
  // perform vmm-swap maintenance (i.e. vmm-swap is already enabled).
  void ReportPolicyResult(VmmSwapPolicyResult policy_result,
                          bool is_enable_request);

  // When SwapVm DBus method tries to enable vmm-swap. This means the vm is idle
  // and ready to enable vmm-swap.
  void OnSwappableIdleEnabled(base::Time time = base::Time::Now());
  // When SwapVm DBus method tries to disable vmm-swap.
  void OnSwappableIdleDisabled();

  // When vmm-swap is enabled.
  void OnVmmSwapEnabled(base::Time time = base::Time::Now());
  // When vmm-swap write pages into disk.
  void OnPreVmmSwapOut(int64_t written_pages,
                       base::Time time = base::Time::Now());
  // When vmm-swap is disabled. Vmm-swap can be disabled not only by SwapVm DBus
  // method but also by low disk signals.
  void OnVmmSwapDisabled(VmmSwapDisableReason reason,
                         base::Time time = base::Time::Now());
  // When ArcVm shutdown.
  void OnDestroy(base::Time time = base::Time::Now());

  // Set FetchVmmSwapStatus
  void SetFetchVmmSwapStatusFunction(FetchVmmSwapStatus func);

 private:
  struct VmmSwapOutMetrics {
    base::Time last_swap_out_time;
    int64_t min_pages_in_file;
    int64_t pages_in_file;
    int64_t total_pages_swapped_in;
    double average_pages_in_file;
    double page_total_duration_in_file_seconds;
    int64_t count_heartbeat;
  };
  void ClearPagesInFileCounters();
  void OnHeartbeat();
  void ReportDisableMetrics(VmmSwapDisableReason reason, base::Time time) const;
  void ReportPagesInFile(base::Time time) const;
  bool SendPagesToUMA(const std::string& unprefixed_metrics_name,
                      int64_t pages) const;
  bool SendDurationToUMA(const std::string& unprefixed_metrics_name,
                         base::TimeDelta duration) const;

  static DisableReasonMetric GetDisableReasonMetric(VmmSwapDisableReason reason,
                                                    bool active);
  static PolicyResultMetric GetPolicyResultMetric(
      VmmSwapPolicyResult policy_result, bool is_maintenance);

  const apps::VmType vm_type_;
  const raw_ref<MetricsLibraryInterface> metrics_
      GUARDED_BY_CONTEXT(sequence_checker_);
  std::unique_ptr<base::RepeatingTimer> heartbeat_timer_
      GUARDED_BY_CONTEXT(sequence_checker_);
  bool is_enabled_ GUARDED_BY_CONTEXT(sequence_checker_) = false;
  std::optional<base::Time> swappable_idle_start_time_
      GUARDED_BY_CONTEXT(sequence_checker_);
  std::optional<base::Time> vmm_swap_enable_time_
      GUARDED_BY_CONTEXT(sequence_checker_);
  std::optional<VmmSwapOutMetrics> vmm_swap_out_metrics_
      GUARDED_BY_CONTEXT(sequence_checker_);
  FetchVmmSwapStatus fetch_vmm_swap_status_
      GUARDED_BY_CONTEXT(sequence_checker_);

  // Ensure calls are made on the right thread.
  SEQUENCE_CHECKER(sequence_checker_);
};

}  // namespace vm_tools::concierge

#endif  // VM_TOOLS_CONCIERGE_VMM_SWAP_METRICS_H_
