// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/mm/balloon_broker.h"

#include <memory>

namespace vm_tools::concierge::mm {

BalloonBroker::BalloonBroker(std::unique_ptr<KillsServer>,
                             scoped_refptr<base::SequencedTaskRunner>) {}

void BalloonBroker::RegisterVm(int, const std::string&) {}

void BalloonBroker::RemoveVm(int) {}

void BalloonBroker::Reclaim(const ReclaimOperation&, ResizePriority) {
  return;
}

ResizePriority BalloonBroker::LowestBalloonBlockPriority() const {
  return ResizePriority::RESIZE_PRIORITY_HIGHEST;
}

}  // namespace vm_tools::concierge::mm
