// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/mm/balloon_blocker.h"

#include <algorithm>

namespace vm_tools::concierge::mm {

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

BalloonBlocker::BalloonBlocker(int vm_cid, std::unique_ptr<Balloon>)
    : vm_cid_(vm_cid) {}

int64_t BalloonBlocker::TryResize(ResizeRequest) {
  return 0;
}

ResizePriority BalloonBlocker::LowestUnblockedPriority(ResizeDirection,
                                                       base::TimeTicks) const {
  return ResizePriority::RESIZE_PRIORITY_LOWEST;
}

}  // namespace vm_tools::concierge::mm
