// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_MM_BALLOON_BLOCKER_H_
#define VM_TOOLS_CONCIERGE_MM_BALLOON_BLOCKER_H_

#include <memory>

#include <vm_memory_management/vm_memory_management.pb.h>

#include "vm_tools/concierge/mm/balloon.h"

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

class BalloonBlocker {
 public:
  BalloonBlocker(int vm_cid, std::unique_ptr<Balloon> balloon);

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

 protected:
  const int vm_cid_;
};

}  // namespace vm_tools::concierge::mm

#endif  // VM_TOOLS_CONCIERGE_MM_BALLOON_BLOCKER_H_
