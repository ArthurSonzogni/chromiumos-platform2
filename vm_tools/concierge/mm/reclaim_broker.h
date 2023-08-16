// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_MM_RECLAIM_BROKER_H_
#define VM_TOOLS_CONCIERGE_MM_RECLAIM_BROKER_H_

#include <memory>

#include <base/files/file_path.h>
#include <base/files/scoped_file.h>

#include <vm_memory_management/vm_memory_management.pb.h>

#include "vm_tools/concierge/mm/balloon_broker.h"
#include "vm_tools/concierge/mm/reclaim_server.h"
#include "vm_tools/concierge/sysfs_notify_watcher.h"

using vm_tools::vm_memory_management::ResizePriority;

namespace vm_tools::concierge::mm {

// The ReclaimBroker receives MGLRU stats updates from VMs and the host and
// performs reclaim operations based on the distribution of the MGLRU
// generations. By default it attempts to balance the age of MGLRU cache among
// the host and all VMs.
class ReclaimBroker {
 public:
  using LowestBalloonBlockPriorityCallback =
      base::RepeatingCallback<ResizePriority(void)>;
  using ReclaimCallback = base::RepeatingCallback<void(
      const BalloonBroker::ReclaimOperation&, ResizePriority)>;

  static std::unique_ptr<ReclaimBroker> Create(
      base::FilePath mglru_path,
      std::unique_ptr<ReclaimServer> reclaim_server,
      LowestBalloonBlockPriorityCallback priority_callback,
      ReclaimCallback reclaim_handler);

  ReclaimBroker(const ReclaimBroker&) = delete;
  ReclaimBroker& operator=(const ReclaimBroker&) = delete;

  // Registers a VM that will be managed by the ReclaimBroker
  void RegisterVm(int vm_cid);

  // Removes a VM context from the reclaim broker.
  void RemoveVm(int vm_cid);
};

}  // namespace vm_tools::concierge::mm

#endif  // VM_TOOLS_CONCIERGE_MM_RECLAIM_BROKER_H_
