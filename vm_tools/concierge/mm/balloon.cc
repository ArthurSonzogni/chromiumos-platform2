// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/mm/balloon.h"

namespace vm_tools::concierge::mm {

Balloon::Balloon(
    int vm_cid,
    const std::string& control_socket,
    scoped_refptr<base::SequencedTaskRunner> balloon_operations_task_runner) {}

void Balloon::SetStallCallback(
    base::RepeatingCallback<void(ResizeResult)> callback) {
  stall_callback_ = callback;
}

void Balloon::DoResize(int64_t, base::OnceCallback<void(ResizeResult)>) {}

base::RepeatingCallback<void(Balloon::ResizeResult)>&
Balloon::GetStallCallback() {
  return stall_callback_;
}

int64_t Balloon::GetTargetSize() {
  return 0;
}

}  // namespace vm_tools::concierge::mm
