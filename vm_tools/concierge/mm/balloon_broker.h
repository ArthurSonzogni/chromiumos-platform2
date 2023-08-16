// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_MM_BALLOON_BROKER_H_
#define VM_TOOLS_CONCIERGE_MM_BALLOON_BROKER_H_

#include <memory>
#include <string>

#include <base/containers/flat_map.h>

#include <vm_memory_management/vm_memory_management.pb.h>

#include "vm_tools/concierge/mm/kills_server.h"

using vm_tools::vm_memory_management::ResizePriority;

namespace vm_tools::concierge::mm {

// The BalloonBroker is the main entrypoint into adjusting the size of
// virtio-balloons managed by the VM Memory Management Service. The
// BalloonBroker must be kept in sync with current VM lifecycle through the
// RegisterVm() and RemoveVm() functions. Callers can query the block
// state of a specific VM's balloon through the BalloonIsBlocked() function and
// can also request to reclaim memory from a specific context (including the
// host) by using the ReclaimFromContext() function. Additionally, the
// BalloonBroker registers itself as the handler of kill decision requests and
// no kill candidate notifications that are received by the
// KillsServer.
class BalloonBroker {
 public:
  explicit BalloonBroker(std::unique_ptr<KillsServer> kills_server);

  BalloonBroker(const BalloonBroker&) = delete;
  BalloonBroker& operator=(const BalloonBroker&) = delete;

  // Registers a VM and the corresponding control socket with the broker.
  void RegisterVm(int vm_cid, const std::string& socket_path);

  // Removes a VM and its corresponding balloon from the broker.
  void RemoveVm(int vm_cid);

  // Returns the lowest ResizePriority that any balloon is blocked at.
  ResizePriority LowestBalloonBlockPriority() const;

  // A reclaim operation consists of reclaim from one or more contexts. This can
  // be represented as a set mapping a CID to a number of bytes to reclaim.
  using ReclaimOperation = base::flat_map<int, size_t>;
  // Performs the specified reclaim operations at |priority|.
  void Reclaim(const ReclaimOperation& reclaim_targets,
               ResizePriority priority);
};

}  // namespace vm_tools::concierge::mm

#endif  // VM_TOOLS_CONCIERGE_MM_BALLOON_BROKER_H_
