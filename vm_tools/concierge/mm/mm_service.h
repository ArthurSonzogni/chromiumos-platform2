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

  // Called to notify the service that a VM has completed it's boot sequence.
  void NotifyVmBootComplete(int vm_cid);

  // Called to notify the service that a VM will stop soon.
  void NotifyVmStopping(int vm_cid);

  // Returns an open FD to the kills server.
  base::ScopedFD GetKillsServerConnection();

 private:
  // Retrieves the lowest priority that won't be blocked from the BalloonBroker.
  ResizePriority GetLowestUnblockedPriority();

  // Instructs the balloon broker to perform the supplied reclaim operation.
  void Reclaim(const BalloonBroker::ReclaimOperation& reclaim_operation,
               ResizePriority priority);

  // Reclaim from the target context until it is blocked at |priority|.
  void ReclaimUntil(int cid, ResizePriority priority);

  // Starts components that run on the negotiation thread.
  bool NegotiationThreadStart(
      scoped_refptr<base::SequencedTaskRunner> balloon_operations_task_runner);

  // Stops the negotiation thread.
  void NegotiationThreadStop();

  // Notify negotiation thread components that a new VM has started.
  void NegotiationThreadNotifyVmStarted(int vm_cid, const std::string& socket);

  // Runs ReclaimUntil on the negotiation thread.
  void NegotiationThreadReclaimUntil(int vm_cid, ResizePriority priority);

  // Notify the negotiation thread that a VM is stopping.
  void NegotiationThreadNotifyVmStopping(int vm_cid);

  // Retrieves the lowest priority that won't be blocked from the BalloonBroker.
  ResizePriority NegotiationThreadGetLowestUnblockedPriority();

  // Instructs the balloon broker to perform the supplied reclaim operation.
  void NegotiationThreadReclaim(
      const BalloonBroker::ReclaimOperation& reclaim_operation,
      ResizePriority priority);

  // TODO(b/254164308) Currently only ARCVM is supported by the
  // MmService
  static constexpr ManagedVmsSet kManagedVms =
      base::MakeFixedFlatSet<apps::VmType, 1>({apps::VmType::ARCVM});

  // Ensure calls are made on the right thread.
  SEQUENCE_CHECKER(sequence_checker_);

  // Ensure negotiation thread calls are only made on the negotiation thread.
  SEQUENCE_CHECKER(negotiation_thread_sequence_checker_);

  // Runs the kills server and handles all balloon negotiations.
  base::Thread negotiation_thread_ GUARDED_BY_CONTEXT(sequence_checker_){
      "VMMMS_Negotiation_Thread"};

  // Used by Balloon instances for running blocking calls to crosvm_control.
  base::Thread balloon_operation_thread_ GUARDED_BY_CONTEXT(sequence_checker_){
      "VMMMS_Balloon_Operation_Thread"};

  // The reclaim broker instance.
  std::unique_ptr<ReclaimBroker> reclaim_broker_
      GUARDED_BY_CONTEXT(sequence_checker_){};

  // The balloon broker instance.
  std::unique_ptr<BalloonBroker> balloon_broker_
      GUARDED_BY_CONTEXT(negotiation_thread_sequence_checker_){};

  // This should be the last member of the class.
  base::WeakPtrFactory<MmService> weak_ptr_factory_;
};

}  // namespace vm_tools::concierge::mm

#endif  // VM_TOOLS_CONCIERGE_MM_MM_SERVICE_H_
