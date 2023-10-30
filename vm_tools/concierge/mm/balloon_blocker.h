// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_MM_BALLOON_BLOCKER_H_
#define VM_TOOLS_CONCIERGE_MM_BALLOON_BLOCKER_H_

#include <memory>
#include <string>

#include <base/containers/flat_map.h>
#include <base/sequence_checker.h>
#include <base/threading/thread.h>
#include <base/time/time.h>
#include <metrics/metrics_library.h>

#include <vm_applications/apps.pb.h>
#include <vm_memory_management/vm_memory_management.pb.h>

#include "vm_tools/concierge/byte_unit.h"
#include "vm_tools/concierge/mm/balloon.h"
#include "vm_tools/concierge/mm/balloon_metrics.h"

namespace vm_tools::concierge::mm {

using vm_tools::vm_memory_management::ResizePriority;

// Represents the direction for a balloon resize.
enum class ResizeDirection { kDeflate, kInflate };

// Represents a request for a single resize of a balloon.
class ResizeRequest {
 public:
  // Creates a ResizeRequest with |priority| and an adjustment size of
  // |delta_bytes|.
  ResizeRequest(ResizePriority priority, int64_t delta_bytes);

  // The direction of this resize.
  ResizeDirection GetDirection() const;

  // Returns the priority of this resize request.
  ResizePriority GetPriority() const;

  // Returns the size delta in bytes for this request.
  int64_t GetDeltaBytes() const;

  // Limits the magnitude of this request to be at most |limit_bytes|. Keeps the
  // direction the same.
  void LimitMagnitude(int64_t limit_bytes);

 private:
  // The priority of this resize.
  const ResizePriority priority_;

  // The size delta in bytes.
  int64_t delta_bytes_ = 0;
};

// The BalloonBlocker is a wrapper for a Balloon that allows for resize priority
// negotiation through ResizeRequests. When a ResizeRequest is received, it
// blocks ResizeRequests of the opposite direction at the same or lower
// priority. Blocked ResizeRequests will not result in any balloon adjustments
// and will return 0 as the balloon delta.
class BalloonBlocker {
 public:
  BalloonBlocker(int vm_cid,
                 std::unique_ptr<Balloon> balloon,
                 std::unique_ptr<BalloonMetrics> metrics,
                 base::TimeDelta low_priority_block_duration =
                     kDefaultLowPriorityBalloonBlockDuration,
                 base::TimeDelta high_priority_block_duration =
                     kDefaultHighPriorityBalloonBlockDuration,
                 base::RepeatingCallback<base::TimeTicks(void)> time_ticks_now =
                     base::BindRepeating(&base::TimeTicks::Now));

  virtual ~BalloonBlocker() = default;

  // Attempts to resize the balloon. The request may be blocked in which case 0
  // will be returned. Returns the actual delta bytes of the balloon.
  // This function is non-blocking. Getting/setting the balloon size is handled
  // by the Balloon class on a separate sequence.
  virtual int64_t TryResize(ResizeRequest request);

  // Returns the lowest priority that is not blocked for |direction| at
  // |check_time|.
  virtual ResizePriority LowestUnblockedPriority(
      ResizeDirection direction, base::TimeTicks check_time) const;

  // Returns the type of VM this blocker is for. Used for logging and metrics.
  apps::VmType GetVmType() const;

  // Sets whether the "BalloonTrace" logs should be printed.
  void SetShouldLogBalloonTrace(bool do_log);

 protected:
  int GetCid();

 private:
  // Used to track the unblock times at each priority.
  using RequestList = base::flat_map<ResizePriority, base::TimeTicks>;

  // Records a received resize request and adds the time to the block list.
  void RecordResizeRequest(const ResizeRequest& request);

  // Run by the Balloon when a balloon stall is detected.
  void OnBalloonStall(Balloon::StallStatistics stats,
                      Balloon::ResizeResult result);

  // Run by the Balloon when a resize finishes.
  void OnResizeResult(ResizePriority priority, Balloon::ResizeResult result);

  // The default duration for a low priority block.
  static constexpr base::TimeDelta kDefaultLowPriorityBalloonBlockDuration =
      base::Seconds(100);

  // The default duration for a high priority block.
  static constexpr base::TimeDelta kDefaultHighPriorityBalloonBlockDuration =
      base::Seconds(10);

  // The highest priority that is blocked at the low priority duration.
  static constexpr ResizePriority kLowPriorityBlockDurationCutoff =
      ResizePriority::RESIZE_PRIORITY_CACHED_TAB;

  // Ensure calls are made on the right sequence.
  SEQUENCE_CHECKER(sequence_checker_);

  // The CID of this balloon's VM.
  const int vm_cid_;

  // The actual balloon.
  const std::unique_ptr<Balloon> balloon_ GUARDED_BY_CONTEXT(sequence_checker_);

  // Metrics logging helpers.
  const std::unique_ptr<BalloonMetrics> metrics_
      GUARDED_BY_CONTEXT(sequence_checker_);

  // The duration of a balloon block.
  // In practice, the duration of the balloon block is the minimum interval for
  // a balloon size re-negotiation at a given priority. If the block duration is
  // small, the balloon will be resized and re-negotiated more often. If the
  // block duration is large, the balloon won't be resized as often, but could
  // come at the cost of unnecessary kills of high priority processes. Because
  // of this, two different block durations are used to allow a longer block
  // duration for low priority processes that don't have much user impact, and a
  // short block duration is used for high priority processes to ensure user
  // impact from kills is minimized at the cost of more balloon resizes when
  // there is higher memory pressure.
  const base::TimeDelta low_priority_block_duration_
      GUARDED_BY_CONTEXT(sequence_checker_);
  const base::TimeDelta high_priority_block_duration_
      GUARDED_BY_CONTEXT(sequence_checker_);

  // Getter for TimeTicks::Now
  const base::RepeatingCallback<base::TimeTicks(void)> time_ticks_now_
      GUARDED_BY_CONTEXT(sequence_checker_);

  // Tracks the unblock time for a direction and priority.
  // TODO(b/307462723): This relies on the ResizePriority enum being dense which
  // makes it difficult to add a new priority. Refactor this to allow for a
  // non-dense ResizePriority enum.
  base::flat_map<ResizeDirection, RequestList> request_lists_
      GUARDED_BY_CONTEXT(sequence_checker_){};

  // Controls whether the "BalloonTrace" logs should be printed.
  bool should_log_balloon_trace_ GUARDED_BY_CONTEXT(sequence_checker_) = true;

  // Must be the last member.
  base::WeakPtrFactory<BalloonBlocker> weak_ptr_factory_{this};
};

}  // namespace vm_tools::concierge::mm

#endif  // VM_TOOLS_CONCIERGE_MM_BALLOON_BLOCKER_H_
