// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef VM_TOOLS_CONCIERGE_BALLOON_POLICY_H_
#define VM_TOOLS_CONCIERGE_BALLOON_POLICY_H_

#include <stdint.h>

#include <string>

#include <crosvm/crosvm_control.h>

#include "vm_tools/concierge/byte_unit.h"

namespace vm_tools::concierge {

struct BalloonStats {
  BalloonStatsFfi stats_ffi;
  uint64_t balloon_actual;
};

struct BalloonWorkingSet {
  BalloonWSFfi working_set_ffi;
  uint64_t balloon_actual;
  static constexpr int64_t kWorkingSetNumBins = 4;

  // Returns total anonymous memory in this working set.
  uint64_t TotalAnonMemory() const {
    uint64_t total = 0;
    for (int i = 0; i < kWorkingSetNumBins; ++i) {
      total += working_set_ffi.ws[i].bytes[0];
    }

    return total;
  }

  // Returns total file-backed memory in this working set.
  uint64_t TotalFileMemory() const {
    uint64_t total = 0;
    for (int i = 0; i < kWorkingSetNumBins; ++i) {
      total += working_set_ffi.ws[i].bytes[1];
    }

    return total;
  }

  // Returns sum of all memory in this working set.
  uint64_t TotalMemory() const { return TotalAnonMemory() + TotalFileMemory(); }
  // Returns anonymous memory count for the given bin in this working set.
  uint64_t AnonMemoryAt(int i) const { return working_set_ffi.ws[i].bytes[0]; }
  // Returns file-backed memory count for the given bin in this working set.
  uint64_t FileMemoryAt(int i) const { return working_set_ffi.ws[i].bytes[1]; }
};

class BalloonPolicyInterface {
 public:
  BalloonPolicyInterface();
  virtual ~BalloonPolicyInterface() = default;

  // Calculates the amount of memory to be shifted between a VM and the host.
  // Positive value means that the policy wants to move that amount of memory
  // from the guest to the host.
  virtual int64_t ComputeBalloonDelta(const BalloonStats& stats,
                                      uint64_t host_available,
                                      const std::string& vm) = 0;

 protected:
  // Returns true if the a balloon trace should be logged.
  bool ShouldLogBalloonTrace(int64_t new_balloon_size);

 private:
  // Do not log a ballon trace if the balloon remains within a window of this
  // width from the previous log.
  int64_t balloon_trace_size_window_width_;

  // The size of the balloon when the last balloon trace was logged.
  int64_t last_balloon_trace_size_ = 0;
};

class BalanceAvailableBalloonPolicy : public BalloonPolicyInterface {
 public:
  BalanceAvailableBalloonPolicy(int64_t critical_host_available,
                                int64_t guest_available_bias,
                                const std::string& vm);

  int64_t ComputeBalloonDelta(const BalloonStats& stats,
                              uint64_t host_available,
                              const std::string& vm) override;

 private:
  // ChromeOS's critical margin.
  const int64_t critical_host_available_;

  // How much to bias the balance of available memory, depending on how full
  // the balloon is.
  const int64_t guest_available_bias_;

  // The max actual balloon size observed.
  int64_t max_balloon_actual_ = 0;

  // This is a guessed value of guest's critical available
  // size. If free memory is smaller than this, guest memory
  // managers (OOM, Android LMKD) will start killing apps.
  int64_t critical_guest_available_;

  // for calculating critical_guest_available
  int64_t prev_guest_available_;
  int64_t prev_balloon_full_percent_;

  // This class keeps the state of a balloon and modified only via
  // ComputeBalloonDelta() so no copy/assign operations are needed.
  BalanceAvailableBalloonPolicy(const BalanceAvailableBalloonPolicy&) = delete;
  BalanceAvailableBalloonPolicy& operator=(
      const BalanceAvailableBalloonPolicy&) = delete;
};

}  // namespace vm_tools::concierge

#endif  // VM_TOOLS_CONCIERGE_BALLOON_POLICY_H_
