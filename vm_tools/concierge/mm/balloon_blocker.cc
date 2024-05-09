// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/mm/balloon_blocker.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

#include <base/logging.h>

#include "vm_tools/concierge/mm/resize_priority.h"

namespace vm_tools::concierge::mm {
namespace {

ResizeDirection OppositeDirection(ResizeDirection direction) {
  return direction == ResizeDirection::kInflate ? ResizeDirection::kDeflate
                                                : ResizeDirection::kInflate;
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

BalloonBlocker::BalloonBlocker(int vm_cid,
                               std::unique_ptr<Balloon> balloon,
                               std::unique_ptr<BalloonMetrics> metrics,
                               base::TimeDelta low_priority_block_duration,
                               base::TimeDelta high_priority_block_duration)
    : vm_cid_(vm_cid),
      balloon_(std::move(balloon)),
      metrics_(std::move(metrics)),
      low_priority_block_duration_(low_priority_block_duration),
      high_priority_block_duration_(high_priority_block_duration) {
  // Initial state should have no blockers.
  ClearBlockersUpToInclusive(HighestResizePriority());

  balloon_->SetStallCallback(base::BindRepeating(
      &BalloonBlocker::OnBalloonStall, weak_ptr_factory_.GetWeakPtr()));
}

int64_t BalloonBlocker::TryResize(ResizeRequest request) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // Even if the resize is not successful, still record the request
  // so the priorities are blocked correctly.
  RecordResizeRequest(request);

  // If the incoming request is a lower priority than the lowest unblocked
  // priority, it is blocked. Do not adjust the balloon.
  if (request.GetPriority() >
      LowestUnblockedPriority(request.GetDirection(), base::TimeTicks::Now())) {
    return 0;
  }

  // Can't deflate below 0, so limit the magnitude of deflations to the current
  // target balloon size.
  if (request.GetDirection() == ResizeDirection::kDeflate) {
    request.LimitMagnitude(balloon_->GetTargetSize());
  }

  // No need to attempt a no-op resize. Return early.
  if (request.GetDeltaBytes() == 0) {
    return 0;
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

  ResizePriority highest_opposite_request = ResizePriority::kInvalid;

  // Iterate in increasing priority order over requests.
  for (ResizePriority check_priority : kAllResizePrioritiesIncreasing) {
    base::TimeTicks unblocked_time = opposite_request_list.at(check_priority);

    // If the unblocked time is after the check time, then the balloon is
    // currently blocked at that priority.
    if (check_time <= unblocked_time) {
      highest_opposite_request = check_priority;
    }
  }

  // If there were no requests in the opposite direction, nothing is blocked, so
  // the lowest priority is unblocked.
  if (highest_opposite_request == ResizePriority::kInvalid) {
    return LowestResizePriority();
  }

  // If everything is blocked, return invalid.
  if (highest_opposite_request == HighestResizePriority()) {
    return ResizePriority::kInvalid;
  }

  // The lowest unblocked priority is one priority level higher than the highest
  // opposite request.
  return static_cast<ResizePriority>(highest_opposite_request - 1);
}

void BalloonBlocker::SetShouldLogBalloonTrace(bool do_log) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (do_log) {
    LOG(INFO) << "Enabling BalloonTrace logs for CID: " << GetCid();
  } else {
    LOG(INFO) << "Disabling BalloonTrace logs for CID: " << GetCid();
  }
  should_log_balloon_trace_ = do_log;
}

void BalloonBlocker::ClearBlockersUpToInclusive(ResizePriority priority) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  for (ResizePriority check_priority : kAllResizePrioritiesIncreasing) {
    if (check_priority < priority) {
      break;
    }

    request_lists_[ResizeDirection::kInflate][check_priority] =
        base::TimeTicks();
    request_lists_[ResizeDirection::kDeflate][check_priority] =
        base::TimeTicks();
  }
}

apps::VmType BalloonBlocker::GetVmType() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return metrics_->GetVmType();
}

int BalloonBlocker::GetCid() {
  return vm_cid_;
}

void BalloonBlocker::RecordResizeRequest(const ResizeRequest& request) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // If the request is for the lowest priority, don't do anything since lowest
  // cannot block anything.
  if (request.GetPriority() == LowestResizePriority()) {
    return;
  }

  base::TimeTicks now = base::TimeTicks::Now();

  RequestList& list = request_lists_[request.GetDirection()];
  ResizePriority requested_priority = request.GetPriority();

  base::TimeDelta block_duration = high_priority_block_duration_;

  // Low priorities have a different block duration.
  if (requested_priority >= kLowPriorityBlockDurationCutoff) {
    block_duration = low_priority_block_duration_;
  }

  // Resize requests can only beat the opposite blocker by 1 level at a time, so
  // cap the priority to the lowest unblocked priority.
  ResizePriority lowest_unblocked_priority =
      LowestUnblockedPriority(request.GetDirection(), now);

  // If the requested priority is lower than a balloon stall but higher than the
  // lowest unblocked priority, cap the priority.
  if (requested_priority < lowest_unblocked_priority &&
      requested_priority > ResizePriority::kBalloonStall) {
    requested_priority = lowest_unblocked_priority;
  }

  // Block at the adjusted requested priority.
  list[requested_priority] = now + block_duration;

  // Additionally unset all blocks at a higher priority than this one.
  for (ResizePriority check_priority : kAllResizePrioritiesIncreasing) {
    if (check_priority >= requested_priority) {
      continue;
    }

    list[check_priority] = base::TimeTicks();
  }
}

void BalloonBlocker::OnBalloonStall(Balloon::StallStatistics stats,
                                    Balloon::ResizeResult result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // If the balloon stalled, block inflations at stall priority.
  RecordResizeRequest({ResizePriority::kBalloonStall, -1});

  OnResizeResult(ResizePriority::kBalloonStall, result);

  metrics_->OnStall(stats);
}

void BalloonBlocker::OnResizeResult(ResizePriority priority,
                                    Balloon::ResizeResult result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (should_log_balloon_trace_) {
    LOG(INFO) << "BalloonTrace:[" << vm_cid_ << "," << priority << ","
              << (result.new_target / MiB(1)) << " MB ("
              << (result.actual_delta_bytes / MiB(1)) << " MB)]";
  }

  metrics_->OnResize(result);
}

}  // namespace vm_tools::concierge::mm
