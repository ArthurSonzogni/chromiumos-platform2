// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_MM_MM_SERVICE_H_
#define VM_TOOLS_CONCIERGE_MM_MM_SERVICE_H_

#include <memory>
#include <string>

#include <base/containers/fixed_flat_set.h>
#include <base/files/scoped_file.h>
#include <base/threading/thread.h>
#include <base/sequence_checker.h>

#include "vm_tools/common/vm_id.h"
#include "vm_tools/concierge/mm/balloon_broker.h"
#include "vm_tools/concierge/mm/reclaim_broker.h"
#include "vm_tools/concierge/vm_util.h"

namespace vm_tools::concierge::mm {

using ManagedVmsSet = base::fixed_flat_set<apps::VmType, 1>;

// The MmService owns and manages all components needed for
// virtio-balloon management of client VMs.
class MmService {
 public:
  // Returns whether the specified VM type is managed by the MmService.
  static constexpr const ManagedVmsSet& ManagedVms() { return kManagedVms; }

  MmService();

  ~MmService();

  // Starts the VM Memory Management Service. Returns true on success, false
  // otherwise.
  bool Start();

  // Called to notify the service that a new VM has started.
  void NotifyVmStarted(apps::VmType type,
                       int vm_cid,
                       const std::string& socket);

  // Called to notify the service that a VM will stop soon.
  void NotifyVmStopping(int vm_cid);

  // Returns an open FD to the kills server.
  base::ScopedFD GetKillsServerConnection();

 private:
  // Retrieves the lowest balloon block priority from the BalloonBroker.
  ResizePriority GetLowestBalloonBlockPriority();

  // Instructs the balloon broker to perform the supplied reclaim operation.
  void Reclaim(const BalloonBroker::ReclaimOperation& reclaim_operation,
               ResizePriority priority);

  // Starts components that run on the balloons thread.
  bool BalloonsThreadStart();

  // Stops the balloons thread.
  void BalloonsThreadStop();

  // Notify balloons thread components that a new VM has started.
  void BalloonsThreadNotifyVmStarted(int vm_cid, const std::string& socket);

  // Notify the balloons thread that a VM is stopping.
  void BalloonsThreadNotifyVmStopping(int vm_cid);

  // Gets the lowest block priority from the balloon broker.
  ResizePriority BalloonsThreadGetLowestBalloonBlockPriority();

  // Instructs the balloon broker to perform the supplied reclaim operation.
  void BalloonsThreadReclaim(
      const BalloonBroker::ReclaimOperation& reclaim_operation,
      ResizePriority priority);

  // TODO(b/254164308) Currently only ARCVM is supported by the
  // MmService
  static constexpr ManagedVmsSet kManagedVms =
      base::MakeFixedFlatSet<apps::VmType, 1>({apps::VmType::ARCVM});

  // Ensure calls are made on the right thread.
  SEQUENCE_CHECKER(sequence_checker_);

  // Ensure balloons thread calls are only made on the balloons thread.
  SEQUENCE_CHECKER(balloons_thread_sequence_checker_);

  // Runs the kills server and handles all balloon modifications.
  base::Thread balloons_thread_ GUARDED_BY_CONTEXT(sequence_checker_){
      "VM_Memory_Management_Balloons_Thread"};

  // The reclaim broker instance.
  std::unique_ptr<ReclaimBroker> reclaim_broker_
      GUARDED_BY_CONTEXT(sequence_checker_){};

  // The balloon broker instance.
  std::unique_ptr<BalloonBroker> balloon_broker_
      GUARDED_BY_CONTEXT(balloons_thread_sequence_checker_){};

  // This should be the last member of the class.
  base::WeakPtrFactory<MmService> weak_ptr_factory_;
};

}  // namespace vm_tools::concierge::mm

#endif  // VM_TOOLS_CONCIERGE_MM_MM_SERVICE_H_
