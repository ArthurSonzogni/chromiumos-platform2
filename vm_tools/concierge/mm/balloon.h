// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_MM_BALLOON_H_
#define VM_TOOLS_CONCIERGE_MM_BALLOON_H_

#include <optional>
#include <string>

#include <base/functional/callback.h>
#include <base/sequence_checker.h>
#include <base/time/time.h>
#include <base/threading/thread.h>

#include "vm_tools/concierge/byte_unit.h"

namespace vm_tools::concierge::mm {

// The Balloon class represents an individual balloon device that belongs to a
// specific VM. The Balloon can be resized through DoResize(). Additionally,
// after being inflated a Balloon tracks its inflation rate to detect if the
// inflation is stalled (indicating the guest VM is very close to OOM). Upon
// detecting a stall, the Balloon will automatically slightly deflate itself and
// run the specified stall callback. All Balloon instances share a thread that
// is used for running blocking operations (such as getting and setting the
// Balloon size through crosvm_control).
class Balloon {
 public:
  struct ResizeResult {
    bool success = false;
    int64_t actual_delta_bytes = 0;
    int64_t new_target = 0;
  };

  struct StallStatistics {
    int64_t inflate_mb_per_s = 0;
  };

  Balloon(
      int vm_cid,
      const std::string& control_socket,
      scoped_refptr<base::SequencedTaskRunner> balloon_operations_task_runner);

  virtual ~Balloon() = default;

  // Sets the callback to be run when the balloon is stalled.
  void SetStallCallback(
      base::RepeatingCallback<void(StallStatistics, ResizeResult)>
          stall_callback);

  // Resizes the balloon by delta_bytes.
  virtual void DoResize(
      int64_t delta_bytes,
      base::OnceCallback<void(ResizeResult)> completion_callback);

  // Non-blocking call that returns the current balloon size target. The balloon
  // may or may not actually be at this size, but should be
  // allocating/deallocating to reach this size.
  virtual int64_t GetTargetSize();

 protected:
  base::RepeatingCallback<void(StallStatistics, ResizeResult)>&
  GetStallCallback();

 private:
  // Performs a resize of the balloon once the current size has been retrieved.
  void DoResizeInternal(
      int64_t delta_bytes,
      base::OnceCallback<void(ResizeResult)> completion_callback,
      std::optional<int64_t> current_size);

  // Runs once setting the balloon size has been completed.
  void OnSetBalloonSizeComplete(
      int64_t original_size,
      int64_t new_balloon_size,
      base::OnceCallback<void(ResizeResult)> completion_callback,
      bool success);

  // Checks if the balloon is at or above the current size.
  bool BalloonIsExpectedSizeOrLarger(int64_t current_size) const;

  // Checks if the balloon is stalled. Returns StallStatistics if the balloon
  // is stalled.
  std::optional<StallStatistics> BalloonIsStalled(int64_t current_size);

  // Both checks for and corrects a balloon stall by backing off on the balloon
  // size if stalled. Returns true if the balloon was stalled, false otherwise.
  void CheckForAndCorrectBalloonStall();
  void CheckForAndCorrectBalloonStallWithSize(
      std::optional<int64_t> current_size);

  // If the time since a resize is less than this amount never treat the balloon
  // as stalled.
  static constexpr base::TimeDelta kBalloonStallDetectionThreshold =
      base::Seconds(4);

  // The interval at which to check for a balloon stall.
  static constexpr base::TimeDelta kBalloonStallDetectionInterval =
      base::Seconds(5);

  // If the balloon inflation rate drops below this amount, treat it as stalled.
  static constexpr int64_t kBalloonStallRateMBps = 15;

  // If the balloon is stalled, deflate it by this amount to relieve memory
  // pressure.
  static constexpr int64_t kBalloonStallBackoffSize = MiB(128);

  // The CID of this balloon's VM.
  const int vm_cid_;

  // The crosvm control socket for this VM.
  const std::string control_socket_;

  // The task runner on which to run balloon operations.
  const scoped_refptr<base::SequencedTaskRunner>
      balloon_operations_task_runner_;

  // Ensure calls are made on the main sequence.
  SEQUENCE_CHECKER(sequence_checker_);

  // Callback to run when a balloon stall is detected.
  base::RepeatingCallback<void(StallStatistics, ResizeResult)> stall_callback_ =
      base::DoNothing();

  // The balloon's size before the most recent resize operation.
  int64_t initial_balloon_size_ GUARDED_BY_CONTEXT(sequence_checker_){};

  // The target balloon size of the most recent resize operation.
  int64_t target_balloon_size_ GUARDED_BY_CONTEXT(sequence_checker_){};

  // The time of the most recent resize operation.
  base::TimeTicks resize_time_ GUARDED_BY_CONTEXT(sequence_checker_){};

  // Whether balloon stall detection is currently running.
  bool checking_balloon_stall_ GUARDED_BY_CONTEXT(sequence_checker_) = false;

  // Must be the last member.
  base::WeakPtrFactory<Balloon> weak_ptr_factory_{this};
};

}  // namespace vm_tools::concierge::mm

#endif  // VM_TOOLS_CONCIERGE_MM_BALLOON_H_
