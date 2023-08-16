// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/mm/reclaim_broker.h"

#include <memory>

#include "vm_tools/concierge/mm/reclaim_server.h"

using vm_tools::vm_memory_management::ResizePriority;

namespace vm_tools::concierge::mm {

// static
std::unique_ptr<ReclaimBroker> ReclaimBroker::Create(
    base::FilePath,
    std::unique_ptr<ReclaimServer>,
    LowestBalloonBlockPriorityCallback,
    ReclaimCallback) {
  return {};
}

void ReclaimBroker::RegisterVm(int) {}

void ReclaimBroker::RemoveVm(int) {}

}  // namespace vm_tools::concierge::mm
