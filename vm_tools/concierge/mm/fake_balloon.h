// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_MM_FAKE_BALLOON_H_
#define VM_TOOLS_CONCIERGE_MM_FAKE_BALLOON_H_

#include "vm_tools/concierge/mm/balloon.h"

#include <string>
#include <vector>

namespace vm_tools::concierge::mm {

class FakeBalloon : public Balloon {
 public:
  FakeBalloon();

  void DoResize(
      int64_t delta_bytes,
      base::OnceCallback<void(ResizeResult)> completion_callback) override;

  void RunStallCallback(StallStatistics stats, ResizeResult result);

  int64_t GetTargetSize() override;

  int64_t target_size_ = 0;

  std::vector<int64_t> resizes_;
  std::vector<ResizeResult> do_resize_results_;
};

}  // namespace vm_tools::concierge::mm

#endif  // VM_TOOLS_CONCIERGE_MM_FAKE_BALLOON_H_
