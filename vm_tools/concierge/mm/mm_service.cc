// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/mm/mm_service.h"

#include <utility>

#include <base/logging.h>

#include <chromeos/constants/vm_tools.h>

#include "vm_tools/concierge/mm/balloon_broker.h"
#include "vm_tools/concierge/mm/kills_server.h"
#include "vm_tools/concierge/mm/reclaim_broker.h"
#include "vm_tools/concierge/mm/reclaim_server.h"
#include "vm_tools/concierge/mm/vm_socket.h"
#include "vm_tools/concierge/thread_utils.h"

namespace vm_tools::concierge::mm {

MmService::MmService(const raw_ref<MetricsLibraryInterface> metrics)
    : metrics_(metrics), weak_ptr_factory_(this) {
  DETACH_FROM_SEQUENCE(negotiation_thread_sequence_checker_);
}

MmService::~MmService() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  negotiation_thread_.task_runner()->PostTask(
      FROM_HERE, base::BindOnce(&MmService::NegotiationThreadStop,
                                weak_ptr_factory_.GetWeakPtr()));

  // Wait for the balloons thread to be cleaned up before continuing.
  negotiation_thread_.Stop();

  // The balloon_operation_thread does not own objects so it does not need to be
  // explicitly stopped. The destructor will implicitly stop it.
}

bool MmService::Start() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  LOG(INFO) << "Starting VM Memory Management Service.";

  if (!negotiation_thread_.StartWithOptions(
          base::Thread::Options(base::MessagePumpType::IO, 0))) {
    LOG(ERROR) << "Failed to start VM Memory Management negotiation thread.";
    return false;
  }

  if (!balloon_operation_thread_.StartWithOptions(
          base::Thread::Options(base::MessagePumpType::IO, 0))) {
    LOG(ERROR)
        << "Failed to start VM Memory Management balloon operation thread.";
    return false;
  }

  // Unretained(this) is safe because this call blocks until the task is
  // complete.
  bool success = PostTaskAndWaitForResult<bool>(
      negotiation_thread_.task_runner(),
      base::BindOnce(&MmService::NegotiationThreadStart, base::Unretained(this),
                     balloon_operation_thread_.task_runner()));

  if (!success) {
    LOG(ERROR) << "Balloons thread failed to start.";
    return false;
  }

  std::unique_ptr<ReclaimServer> reclaim_server =
      std::make_unique<ReclaimServer>(kVmMemoryManagementReclaimServerPort);

  if (!reclaim_server->StartListening()) {
    LOG(ERROR)
        << "VM Memory Management reclaim server failed to start listening.";
    return false;
  }

  // Unretained(this) is safe because the reclaim_broker_ instance is owned by
  // this class and is destroyed when this class is destroyed.
  reclaim_broker_ = ReclaimBroker::Create(
      {base::FilePath("/sys/kernel/mm/lru_gen/admin"),
       std::move(reclaim_server),
       base::BindRepeating(&MmService::GetLowestUnblockedPriority,
                           base::Unretained(this)),
       base::BindRepeating(&MmService::Reclaim, base::Unretained(this))});

  if (!reclaim_broker_) {
    LOG(ERROR) << "Failed to create reclaim broker.";
    return false;
  }

  return true;
}

void MmService::NotifyVmStarted(apps::VmType vm_type,
                                int vm_cid,
                                const std::string& socket) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!ManagedVms().contains(vm_type)) {
    return;
  }

  reclaim_broker_->RegisterVm(vm_cid);

  negotiation_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(&MmService::NegotiationThreadNotifyVmStarted,
                     weak_ptr_factory_.GetWeakPtr(), vm_type, vm_cid, socket));
}

void MmService::NotifyVmBootComplete(int vm_cid) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // When a VM has completed boot, slowly reclaim from it until it starts to
  // kill low priority apps or a new MGLRU generation is created. This helps
  // ensure that future balloon inflations resulting from host kills will
  // actually apply memory pressure in the guest.
  ReclaimUntilBlocked(vm_cid, ResizePriority::RESIZE_PRIORITY_MGLRU_RECLAIM,
                      base::DoNothing());
}

void MmService::NotifyVmStopping(int vm_cid) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  reclaim_broker_->RemoveVm(vm_cid);

  negotiation_thread_.task_runner()->PostTask(
      FROM_HERE, base::BindOnce(&MmService::NegotiationThreadNotifyVmStopping,
                                weak_ptr_factory_.GetWeakPtr(), vm_cid));
}

base::ScopedFD MmService::GetKillsServerConnection() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  VmSocket socket;
  if (!socket.Connect(kVmMemoryManagementKillsServerPort)) {
    return {};
  }

  return socket.Release();
}

void MmService::ReclaimUntilBlocked(int vm_cid,
                                    ResizePriority priority,
                                    ReclaimUntilBlockedCallback cb) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto wrapped_cb = base::BindOnce(
      [](ReclaimUntilBlockedCallback cb, bool success, const char* err_msg) {
        base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
            FROM_HERE, base::BindOnce(std::move(cb), success, err_msg));
      },
      std::move(cb));

  negotiation_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(&MmService::NegotiationThreadReclaimUntilBlocked,
                     weak_ptr_factory_.GetWeakPtr(), vm_cid, priority,
                     std::move(wrapped_cb)));
}

void MmService::StopReclaimUntilBlocked(int vm_cid) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  negotiation_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(&MmService::NegotiationThreadStopReclaimUntilBlocked,
                     weak_ptr_factory_.GetWeakPtr(), vm_cid));
}

ResizePriority MmService::GetLowestUnblockedPriority() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // Unretained(this) is safe because this call blocks until the task is
  // complete.
  return PostTaskAndWaitForResult<ResizePriority>(
      negotiation_thread_.task_runner(),
      base::BindOnce(&MmService::NegotiationThreadGetLowestUnblockedPriority,
                     base::Unretained(this)));
}

void MmService::Reclaim(
    const BalloonBroker::ReclaimOperation& reclaim_operation,
    ResizePriority priority) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  negotiation_thread_.task_runner()->PostTask(
      FROM_HERE, base::BindOnce(&MmService::NegotiationThreadReclaim,
                                weak_ptr_factory_.GetWeakPtr(),
                                reclaim_operation, priority));
}

bool MmService::NegotiationThreadStart(
    scoped_refptr<base::SequencedTaskRunner> balloon_operations_task_runner) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(negotiation_thread_sequence_checker_);
  LOG(INFO) << "Starting VM Memory Management Kills Server.";

  std::unique_ptr<KillsServer> kills_server =
      std::make_unique<KillsServer>(kVmMemoryManagementKillsServerPort);

  if (!kills_server->StartListening()) {
    LOG(ERROR) << "Kills server failed to start listening.";
    return false;
  }

  balloon_broker_ = std::make_unique<BalloonBroker>(
      std::move(kills_server), balloon_operations_task_runner, metrics_);

  return true;
}

void MmService::NegotiationThreadStop() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(negotiation_thread_sequence_checker_);
  balloon_broker_.reset();
  DETACH_FROM_SEQUENCE(negotiation_thread_sequence_checker_);
}

void MmService::NegotiationThreadNotifyVmStarted(apps::VmType vm_type,
                                                 int vm_cid,
                                                 const std::string& socket) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(negotiation_thread_sequence_checker_);
  balloon_broker_->RegisterVm(vm_type, vm_cid, socket);
}

void MmService::NegotiationThreadReclaimUntilBlocked(
    int vm_cid, ResizePriority priority, ReclaimUntilBlockedCallback cb) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(negotiation_thread_sequence_checker_);

  balloon_broker_->ReclaimUntilBlocked(vm_cid, priority, std::move(cb));
}

void MmService::NegotiationThreadStopReclaimUntilBlocked(int vm_cid) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(negotiation_thread_sequence_checker_);

  balloon_broker_->StopReclaimUntilBlocked(vm_cid);
}

void MmService::NegotiationThreadNotifyVmStopping(int vm_cid) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(negotiation_thread_sequence_checker_);
  balloon_broker_->RemoveVm(vm_cid);
}

ResizePriority MmService::NegotiationThreadGetLowestUnblockedPriority() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(negotiation_thread_sequence_checker_);
  return balloon_broker_->LowestUnblockedPriority();
}

void MmService::NegotiationThreadReclaim(
    const BalloonBroker::ReclaimOperation& reclaim_operation,
    ResizePriority priority) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(negotiation_thread_sequence_checker_);
  balloon_broker_->Reclaim(reclaim_operation, priority);
}

}  // namespace vm_tools::concierge::mm
