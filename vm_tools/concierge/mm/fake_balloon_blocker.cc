// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/mm/fake_balloon_blocker.h"

#include <memory>

namespace vm_tools::concierge::mm {

// static
base::flat_map<int, FakeBalloonBlocker*>
    FakeBalloonBlocker::fake_balloon_blockers_{};

FakeBalloonBlocker::FakeBalloonBlocker(int vm_cid)
    : BalloonBlocker(vm_cid, {}) {
  fake_balloon_blockers_[vm_cid] = this;
}

FakeBalloonBlocker::~FakeBalloonBlocker() {
  fake_balloon_blockers_.erase(vm_cid_);
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

  while (priority <= ResizePriority::RESIZE_PRIORITY_LOWEST) {
    blocks_.at(direction)[priority] = true;

    priority = static_cast<ResizePriority>(priority + 1);
  }
}

ResizePriority FakeBalloonBlocker::LowestUnblockedPriority(
    ResizeDirection direction, base::TimeTicks check_time) const {
  if (!blocks_.contains(direction)) {
    return ResizePriority::RESIZE_PRIORITY_LOWEST;
  }

  for (auto block : blocks_.at(direction)) {
    if (block.second) {
      return static_cast<ResizePriority>(block.first - 1);
    }
  }

  return ResizePriority::RESIZE_PRIORITY_LOWEST;
}

int FakeBalloonBlocker::Cid() {
  return vm_cid_;
}

}  // namespace vm_tools::concierge::mm
