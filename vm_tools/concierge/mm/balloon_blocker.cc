// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/mm/balloon_blocker.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

#include <base/logging.h>

using vm_tools::vm_memory_management::ResizePriority_Name;

namespace vm_tools::concierge::mm {
namespace {

ResizeDirection OppositeDirection(ResizeDirection direction) {
  return direction == ResizeDirection::kInflate ? ResizeDirection::kDeflate
                                                : ResizeDirection::kInflate;
}

void ForEachResizePriorityIncreasing(
    std::function<void(ResizePriority)> callback) {
  ResizePriority priority = ResizePriority::RESIZE_PRIORITY_LOWEST;
  while (priority >= ResizePriority::RESIZE_PRIORITY_HIGHEST) {
    callback(priority);
    priority = static_cast<ResizePriority>(priority - 1);
  }
}

}  // namespace

ResizeRequest::ResizeRequest(ResizePriority priority, int64_t delta_bytes)
    : priority_(priority), delta_bytes_(delta_bytes) {}

ResizeDirection ResizeRequest::GetDirection() const {
  return delta_bytes_ < 0 ? ResizeDirection::kDeflate
                          : ResizeDirection::kInflate;
}

ResizePriority ResizeRequest::GetPriority() const {
  return priority_;
}

int64_t ResizeRequest::GetDeltaBytes() const {
  return delta_bytes_;
}

void ResizeRequest::LimitMagnitude(int64_t limit_bytes) {
  int64_t magnitude = std::abs(GetDeltaBytes());

  magnitude = std::min(magnitude, std::abs(limit_bytes));

  delta_bytes_ =
      GetDirection() == ResizeDirection::kInflate ? magnitude : -magnitude;
}

BalloonBlocker::BalloonBlocker(
    int vm_cid,
    std::unique_ptr<Balloon> balloon,
    base::TimeDelta low_priority_block_duration,
    base::TimeDelta high_priority_block_duration,
    base::RepeatingCallback<base::TimeTicks(void)> time_ticks_now)
    : vm_cid_(vm_cid),
      balloon_(std::move(balloon)),
      low_priority_block_duration_(low_priority_block_duration),
      high_priority_block_duration_(high_priority_block_duration),
      time_ticks_now_(time_ticks_now) {
  // Initialize all the request lists to the unblocked state.
  RequestList fully_unblocked{};

  ForEachResizePriorityIncreasing([&fully_unblocked](ResizePriority priority) {
    fully_unblocked[priority] = {};
  });

  request_lists_[ResizeDirection::kInflate] = fully_unblocked;
  request_lists_[ResizeDirection::kDeflate] = fully_unblocked;

  balloon_->SetStallCallback(base::BindRepeating(
      &BalloonBlocker::OnBalloonStall, weak_ptr_factory_.GetWeakPtr()));
}

int64_t BalloonBlocker::TryResize(ResizeRequest request) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // Even if the resize is not successful, still record the request
  // so the priorities are blocked correctly.
  RecordResizeRequest(request);

  if (request.GetPriority() >
      LowestUnblockedPriority(request.GetDirection(), time_ticks_now_.Run())) {
    return 0;
  }

  // Can't deflate below 0, so limit the magnitude of deflations to the current
  // target balloon size.
  if (request.GetDirection() == ResizeDirection::kDeflate) {
    request.LimitMagnitude(balloon_->GetTargetSize());
  }

  balloon_->DoResize(
      request.GetDeltaBytes(),
      base::BindOnce(&BalloonBlocker::OnResizeResult,
                     weak_ptr_factory_.GetWeakPtr(), request.GetPriority()));

  return request.GetDeltaBytes();
}

ResizePriority BalloonBlocker::LowestUnblockedPriority(
    ResizeDirection direction, base::TimeTicks check_time) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  const RequestList& opposite_request_list =
      request_lists_.at(OppositeDirection(direction));

  ResizePriority highest_opposite_request =
      ResizePriority::RESIZE_PRIORITY_N_PRIORITIES;

  // Iterate in increasing priority order over requests.
  ForEachResizePriorityIncreasing([&opposite_request_list, &check_time,
                                   &highest_opposite_request](
                                      ResizePriority check_priority) {
    base::TimeTicks unblocked_time = opposite_request_list.at(check_priority);

    // If the unblocked time is after the check time, then the balloon is
    // currently blocked at that priority.
    if (check_time <= unblocked_time) {
      highest_opposite_request = check_priority;
    }
  });

  return static_cast<ResizePriority>(highest_opposite_request - 1);
}

int BalloonBlocker::GetCid() {
  return vm_cid_;
}

void BalloonBlocker::RecordResizeRequest(const ResizeRequest& request) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // If the request is for the lowest priority, don't do anything since lowest
  // cannot block anything.
  if (request.GetPriority() == ResizePriority::RESIZE_PRIORITY_LOWEST) {
    return;
  }

  base::TimeTicks now = time_ticks_now_.Run();

  RequestList& list = request_lists_[request.GetDirection()];
  ResizePriority requested_priority = request.GetPriority();

  // Resize requests can only beat the opposite blocker by 1 level at a time, so
  // cap the priority to the lowest unblocked priority.
  ResizePriority lowest_unblocked_priority =
      LowestUnblockedPriority(request.GetDirection(), now);

  // If the requested priority is lower than a balloon stall but higher than the
  // lowest unblocked priority, cap the priority.
  if (requested_priority < lowest_unblocked_priority &&
      requested_priority > ResizePriority::RESIZE_PRIORITY_BALLOON_STALL) {
    requested_priority = lowest_unblocked_priority;
  }

  base::TimeDelta block_duration = high_priority_block_duration_;

  // Low priorities have a different block duration.
  if (requested_priority >= kLowPriorityBlockDurationCutoff) {
    block_duration = low_priority_block_duration_;
  }

  // Block at the adjusted requested priority.
  list[requested_priority] = now + block_duration;

  // Additionally unset all blocks at a higher priority than this one.
  ForEachResizePriorityIncreasing(
      [&list, requested_priority](ResizePriority check_priority) {
        if (check_priority >= requested_priority) {
          return;
        }

        list[check_priority] = base::TimeTicks();
      });
}

void BalloonBlocker::OnBalloonStall(Balloon::ResizeResult result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // If the balloon stalled, block inflations at stall priority.
  RecordResizeRequest({ResizePriority::RESIZE_PRIORITY_BALLOON_STALL, -1});

  OnResizeResult(ResizePriority::RESIZE_PRIORITY_BALLOON_STALL, result);
}

void BalloonBlocker::OnResizeResult(ResizePriority priority,
                                    Balloon::ResizeResult result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  LOG(INFO) << "BalloonTrace:[" << vm_cid_ << ","
            << ResizePriority_Name(priority) << ","
            << (result.new_target / MiB(1)) << " MB ("
            << (result.actual_delta_bytes / MiB(1)) << " MB)]";
}

}  // namespace vm_tools::concierge::mm
