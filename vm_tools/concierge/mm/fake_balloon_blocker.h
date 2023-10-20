// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_MM_FAKE_BALLOON_BLOCKER_H_
#define VM_TOOLS_CONCIERGE_MM_FAKE_BALLOON_BLOCKER_H_

#include "vm_tools/concierge/mm/balloon_blocker.h"

#include <memory>
#include <vector>

#include <base/containers/flat_map.h>
namespace vm_tools::concierge::mm {

class FakeBalloonBlocker : public BalloonBlocker {
 public:
  static base::flat_map<int, FakeBalloonBlocker*> fake_balloon_blockers_;

  FakeBalloonBlocker(int vm_cid, std::unique_ptr<BalloonMetrics> metrics);

  ~FakeBalloonBlocker();

  int64_t TryResize(ResizeRequest request) override;

  ResizePriority LowestUnblockedPriority(
      ResizeDirection direction, base::TimeTicks check_time) const override;

  int Cid();

  void BlockAt(ResizeDirection direction, ResizePriority priority);

  std::vector<ResizeRequest> resize_requests_;
  std::vector<int64_t> try_resize_results_;
  base::flat_map<ResizeDirection, base::flat_map<ResizePriority, bool>>
      blocks_{};
};

}  // namespace vm_tools::concierge::mm

#endif  // VM_TOOLS_CONCIERGE_MM_FAKE_BALLOON_BLOCKER_H_
