// Copyright 2016 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_COMMON_CPU_LIMITER_H_
#define UPDATE_ENGINE_COMMON_CPU_LIMITER_H_

#include <brillo/message_loops/message_loop.h>

namespace chromeos_update_engine {

// Cgroups cpu shares constants. 1024 is the default shares a standard process
// gets and 2 is the minimum value. We set High as a value that gives the
// update-engine 2x the cpu share of a standard process.
enum class CpuShares : int {
  kHigh = 2048,
  kNormal = 1024,
  kLow = 2,
};

class CPULimiter {
 public:
  CPULimiter() = default;
  ~CPULimiter();

  // Sets the cpu shares to low and sets up timeout events to stop the limiter.
  void StartLimiter();

  // Resets the cpu shares to normal and destroys any scheduled timeout sources.
  void StopLimiter();

  // Sets the cpu shares to |shares|. This method can be user at any time, but
  // if the limiter is not running, the shares won't be reset to normal.
  bool SetCpuShares(CpuShares shares);

 private:
  // The cpu shares timeout source callback sets the current cpu shares to
  // normal.
  void StopLimiterCallback();

  // Current cpu shares.
  CpuShares shares_ = CpuShares::kNormal;

  // The cpu shares management timeout task id.
  brillo::MessageLoop::TaskId manage_shares_id_{
      brillo::MessageLoop::kTaskIdNull};
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_COMMON_CPU_LIMITER_H_
