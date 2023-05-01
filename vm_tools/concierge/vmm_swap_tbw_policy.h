// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_VMM_SWAP_TBW_POLICY_H_
#define VM_TOOLS_CONCIERGE_VMM_SWAP_TBW_POLICY_H_

#include <cstdint>
#include <utility>

#include <base/time/time.h>
#include <base/containers/ring_buffer.h>
#include <base/sequence_checker.h>

namespace vm_tools::concierge {

// VmmSwapTbwPolicy tracks the TBW (Total Bytes Written) from vmm-swap feature
// and decides whether it is able to swap out or not based on 28 days history
// not to exceeds the given target.
//
// Managing TBW is important because because swapping out too many memory into
// the swap file damages the disk.
//
// VmmSwapTbwPolicy is not thread-safe.
class VmmSwapTbwPolicy final {
 public:
  VmmSwapTbwPolicy();
  VmmSwapTbwPolicy(const VmmSwapTbwPolicy&) = delete;
  VmmSwapTbwPolicy& operator=(const VmmSwapTbwPolicy&) = delete;
  ~VmmSwapTbwPolicy() = default;

  // Set the target tbw per day.
  void SetTargetTbwPerDay(uint64_t target_tbw_per_day);

  // Record a tbw history entry.
  //
  // The given `time` is expected to be later than previous Record() calls.
  // The `time` is injectable for testing purpose.
  void Record(uint64_t bytes_written, base::Time time = base::Time::Now());

  // Returns whether it is able to vmm-swap out the guest memory in terms of
  // TBW.
  //
  // The `time` is injectable for testing purpose.
  bool CanSwapOut(base::Time time = base::Time::Now()) const;

 private:
  static constexpr size_t kTbwHistoryLength = 28;

  uint64_t target_tbw_per_day_ GUARDED_BY_CONTEXT(sequence_checker_) = 0;
  base::RingBuffer<std::pair<base::Time, uint64_t>, kTbwHistoryLength>
      tbw_history_ GUARDED_BY_CONTEXT(sequence_checker_);

  // Ensure calls are made on the right thread.
  SEQUENCE_CHECKER(sequence_checker_);
};

}  // namespace vm_tools::concierge

#endif  // VM_TOOLS_CONCIERGE_VMM_SWAP_TBW_POLICY_H_
