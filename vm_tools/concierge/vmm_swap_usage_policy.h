// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_VMM_SWAP_USAGE_POLICY_H_
#define VM_TOOLS_CONCIERGE_VMM_SWAP_USAGE_POLICY_H_

#include <optional>
#include <utility>

#include <base/containers/ring_buffer.h>
#include <base/sequence_checker.h>
#include <base/time/time.h>

namespace vm_tools::concierge {

// Predicts the time when vmm-swap will be disabled using last 4 weeks of
// history.
//
// If it can estimate that vmm-swap will be disabled soon, it is not worth to
// enable vmm-swap so that we can save the TBW (total bytes written).
//
// The vmm-swap is enabled when no application exists on ARCVM and disabled when
// the user launches an application. Enable/disable events should have patterns
// from the user's weekly behavior and be predictable.
//
// The policy projects the the vmm-swap usage patterns from each of the previous
// four weeks onto the current week and calculates how long swap would be
// disabled in each case. The final predicted value is the average of those
// calculated values.
class VmmSwapUsagePolicy final {
 public:
  VmmSwapUsagePolicy() = default;
  VmmSwapUsagePolicy(const VmmSwapUsagePolicy&) = delete;
  VmmSwapUsagePolicy& operator=(const VmmSwapUsagePolicy&) = delete;
  ~VmmSwapUsagePolicy() = default;

  void OnEnabled(base::Time time = base::Time::Now());
  void OnDisabled(base::Time time = base::Time::Now());
  // Predict when vmm-swap will be disabled.
  //
  // This returns the duration from the now. The parameter `now` is injectable
  // for mainly testing purpose.
  base::TimeDelta PredictDuration(base::Time now = base::Time::Now());

 private:
  struct SwapPeriod {
    base::Time start;
    std::optional<base::TimeDelta> duration;
  };
  static constexpr int kUsageHistoryNumWeeks = 4;
  // The length of the history ring buffer. The history is hourly and at most 4
  // weeks (24 hours * 7 days * 4 weeks).
  static constexpr size_t kUsageHistoryLength = 24 * 7 * kUsageHistoryNumWeeks;

  base::RingBuffer<SwapPeriod, kUsageHistoryLength> usage_history_
      GUARDED_BY_CONTEXT(sequence_checker_);
  bool is_enabled_ GUARDED_BY_CONTEXT(sequence_checker_) = false;

  void AddEnableRecordIfMissing(base::Time time);

  // Ensure calls are made on the right thread.
  SEQUENCE_CHECKER(sequence_checker_);
};

}  // namespace vm_tools::concierge

#endif  // VM_TOOLS_CONCIERGE_VMM_SWAP_USAGE_POLICY_H_
