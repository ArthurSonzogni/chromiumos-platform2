// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/mm/fake_balloon.h"

#include <memory>
#include <utility>

#include <base/task/sequenced_task_runner.h>

namespace vm_tools::concierge::mm {

FakeBalloon::FakeBalloon()
    : Balloon(0, {}, base::SequencedTaskRunner::GetCurrentDefault()) {}

void FakeBalloon::DoResize(
    int64_t delta_bytes,
    base::OnceCallback<void(ResizeResult)> completion_callback) {
  target_size_ += delta_bytes;
  resizes_.emplace_back(delta_bytes);

  ResizeResult result{};

  if (do_resize_results_.size() != 0) {
    result = do_resize_results_.back();
    do_resize_results_.pop_back();
  }

  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(std::move(completion_callback), result));
}

void FakeBalloon::RunStallCallback(StallStatistics stats, ResizeResult result) {
  GetStallCallback().Run(stats, result);
}

int64_t FakeBalloon::GetTargetSize() {
  return target_size_;
}

}  // namespace vm_tools::concierge::mm
