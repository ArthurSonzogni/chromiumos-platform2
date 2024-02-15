// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/mm/fake_balloon_blocker.h"

#include <utility>

#include "vm_tools/concierge/mm/fake_balloon.h"

namespace vm_tools::concierge::mm {

// static
base::flat_map<int, FakeBalloonBlocker*>
    FakeBalloonBlocker::fake_balloon_blockers_{};

FakeBalloonBlocker::FakeBalloonBlocker(int vm_cid,
                                       std::unique_ptr<BalloonMetrics> metrics)
    : BalloonBlocker(
          vm_cid, std::make_unique<FakeBalloon>(), std::move(metrics)) {
  fake_balloon_blockers_[vm_cid] = this;

  blocks_[ResizeDirection::kInflate] = {};
  blocks_[ResizeDirection::kDeflate] = {};

  for (ResizePriority priority : kAllResizePrioritiesIncreasing) {
    blocks_[ResizeDirection::kInflate][priority] = false;
    blocks_[ResizeDirection::kDeflate][priority] = false;
  }
}

FakeBalloonBlocker::~FakeBalloonBlocker() {
  fake_balloon_blockers_.erase(GetCid());
}

int64_t FakeBalloonBlocker::TryResize(ResizeRequest request) {
  resize_requests_.emplace_back(request);

  if (try_resize_results_.size() == 0) {
    return 0;
  }

  int64_t result = try_resize_results_.back();
  try_resize_results_.pop_back();
  return result;
}

void FakeBalloonBlocker::BlockAt(ResizeDirection direction,
                                 ResizePriority priority) {
  if (!blocks_.contains(direction)) {
    blocks_[direction] = {};
  }

  for (ResizePriority check_priority : kAllResizePrioritiesIncreasing) {
    blocks_.at(direction)[check_priority] = priority <= check_priority;
  }
}

ResizePriority FakeBalloonBlocker::LowestUnblockedPriority(
    ResizeDirection direction, base::TimeTicks check_time) const {
  for (ResizePriority priority : kAllResizePrioritiesIncreasing) {
    if (!blocks_.at(direction).at(priority)) {
      return priority;
    }
  }

  return ResizePriority::kInvalid;
}

int FakeBalloonBlocker::Cid() {
  return GetCid();
}

void FakeBalloonBlocker::ClearBlockersUpToInclusive(ResizePriority priority) {
  clear_blockers_priority_ = priority;
}

}  // namespace vm_tools::concierge::mm
