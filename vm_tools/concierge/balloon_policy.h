// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef VM_TOOLS_CONCIERGE_BALLOON_POLICY_H_
#define VM_TOOLS_CONCIERGE_BALLOON_POLICY_H_

#include <stdint.h>

namespace vm_tools {
namespace concierge {

constexpr int64_t KIB = 1024;
constexpr int64_t MIB = 1024 * 1024;

struct BalloonStats {
  int64_t available_memory;
  int64_t balloon_actual;
  int64_t disk_caches;
  int64_t free_memory;
  int64_t major_faults;
  int64_t minor_faults;
  int64_t swap_in;
  int64_t swap_out;
  int64_t total_memory;
};

struct BalloonPolicyParams {
  // values referenced by the policy

  // retrieved from ballon stats
  int64_t actual_balloon_size;

  // Originally retrieved from
  // /sys/kernel/mm/chromeos-low_mem/margin in crosvm.
  // In concierge, resourced will provide this value via
  // GetMemoryMarginKB::critical_margin
  int64_t critical_host_available;

  // passed on launching crosvm
  int64_t guest_available_bias;

  // retrieved from balloon stats
  int64_t guest_cached;

  // retrieved from balloon stats
  int64_t guest_free;

  // /sys/kernel/mm/chromeos-low_mem/available or provided by
  // resourced via Get[Foreground]AvailableMemoryKB::available
  int64_t host_available;

  static BalloonPolicyParams FromBalloonStats(const BalloonStats& stats,
                                              int64_t critical_host_available,
                                              int64_t guest_available_bias,
                                              int64_t host_available) {
    BalloonPolicyParams params;
    params.actual_balloon_size = stats.balloon_actual;
    params.critical_host_available = critical_host_available;
    params.guest_available_bias = guest_available_bias;
    params.guest_cached = stats.disk_caches;
    params.guest_free = stats.free_memory;
    params.host_available = host_available;
    return params;
  }
};

class BalloonPolicy {
 public:
  BalloonPolicy() : critical_guest_available_(400 * MIB) {}

  // Calculates the amount of memory to be shifted between a VM and the host.
  // Positive value means that the policy wants to move that amount of memory
  // from the guest to the host.
  int64_t ComputeBalloonDelta(const BalloonPolicyParams&);

 private:
  // The max actual balloon size observed.
  int64_t max_balloon_actual_;

  // This is a guessed value of guest's critical available
  // size. If free memory is smaller than this, guest memory
  // managers (OOM, Android LMKD) will start killing apps.
  int64_t critical_guest_available_;

  // for calculating critical_guest_available
  int64_t prev_guest_available_;
  int64_t prev_balloon_full_percent_;

  // This class keeps the state of a balloon and modified only via
  // ComputeBalloonDelta() so no copy/assign operations are needed.
  BalloonPolicy(const BalloonPolicy&) = delete;
  BalloonPolicy& operator=(const BalloonPolicy&) = delete;
};

}  // namespace concierge
}  // namespace vm_tools

#endif  // VM_TOOLS_CONCIERGE_BALLOON_POLICY_H_
