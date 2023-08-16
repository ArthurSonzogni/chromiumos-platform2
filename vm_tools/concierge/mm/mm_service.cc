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

namespace vm_tools::concierge::mm {
namespace {
template <typename T>
T PostTaskAndWaitForResult(scoped_refptr<base::TaskRunner> task_runner,
                           base::OnceCallback<T()> func) {
  base::WaitableEvent event{};
  T result;

  task_runner->PostTask(
      FROM_HERE, base::BindOnce(
                     [](base::OnceCallback<T()> callback,
                        raw_ref<base::WaitableEvent> event, raw_ref<T> result) {
                       *result = std::move(callback).Run();
                       event->Signal();
                     },
                     std::move(func), raw_ref(event), raw_ref(result)));

  event.Wait();
  return result;
}
}  // namespace

MmService::MmService() : weak_ptr_factory_(this) {
  DETACH_FROM_SEQUENCE(balloons_thread_sequence_checker_);
}

MmService::~MmService() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  balloons_thread_.task_runner()->PostTask(
      FROM_HERE, base::BindOnce(&MmService::BalloonsThreadStop,
                                weak_ptr_factory_.GetWeakPtr()));

  // Wait for the balloons thread to be cleaned up before continuing.
  balloons_thread_.Stop();
}

bool MmService::Start() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  LOG(INFO) << "Starting VM Memory Management Service.";

  if (!balloons_thread_.StartWithOptions(
          base::Thread::Options(base::MessagePumpType::IO, 0))) {
    LOG(ERROR) << "Failed to start VM Memory Management balloons thread.";
    return false;
  }

  // Unretained(this) is safe because this call blocks until the task is
  // complete.
  bool success = PostTaskAndWaitForResult<bool>(
      balloons_thread_.task_runner(),
      base::BindOnce(&MmService::BalloonsThreadStart, base::Unretained(this)));

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
      base::FilePath("/sys/kernel/mm/lru_gen/admin"), std::move(reclaim_server),
      base::BindRepeating(&MmService::GetLowestBalloonBlockPriority,
                          base::Unretained(this)),
      base::BindRepeating(&MmService::Reclaim, base::Unretained(this)));

  if (!reclaim_broker_) {
    LOG(ERROR) << "Failed to create reclaim broker.";
    return false;
  }

  return true;
}

void MmService::NotifyVmStarted(apps::VmType type,
                                int vm_cid,
                                const std::string& socket) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!ManagedVms().contains(type)) {
    return;
  }

  reclaim_broker_->RegisterVm(vm_cid);

  balloons_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(&MmService::BalloonsThreadNotifyVmStarted,
                     weak_ptr_factory_.GetWeakPtr(), vm_cid, socket));
}

void MmService::NotifyVmStopping(int vm_cid) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  reclaim_broker_->RemoveVm(vm_cid);

  balloons_thread_.task_runner()->PostTask(
      FROM_HERE, base::BindOnce(&MmService::BalloonsThreadNotifyVmStopping,
                                weak_ptr_factory_.GetWeakPtr(), vm_cid));
}

base::ScopedFD MmService::GetKillsServerConnection(uint32_t read_timeout_ms) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  VmSocket socket;
  if (!socket.Connect(kVmMemoryManagementKillsServerPort, read_timeout_ms)) {
    return {};
  }

  return socket.Release();
}

ResizePriority MmService::GetLowestBalloonBlockPriority() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // Unretained(this) is safe because this call blocks until the task is
  // complete.
  return PostTaskAndWaitForResult<ResizePriority>(
      balloons_thread_.task_runner(),
      base::BindOnce(&MmService::BalloonsThreadGetLowestBalloonBlockPriority,
                     base::Unretained(this)));
}

void MmService::Reclaim(
    const BalloonBroker::ReclaimOperation& reclaim_operation,
    ResizePriority priority) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  balloons_thread_.task_runner()->PostTask(
      FROM_HERE, base::BindOnce(&MmService::BalloonsThreadReclaim,
                                weak_ptr_factory_.GetWeakPtr(),
                                reclaim_operation, priority));
}

bool MmService::BalloonsThreadStart() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(balloons_thread_sequence_checker_);
  LOG(INFO) << "Starting VM Memory Management Kills Server.";

  std::unique_ptr<KillsServer> kills_server =
      std::make_unique<KillsServer>(kVmMemoryManagementKillsServerPort);

  if (!kills_server->StartListening()) {
    LOG(ERROR) << "Kills server failed to start listening.";
    return false;
  }

  balloon_broker_ = std::make_unique<BalloonBroker>(std::move(kills_server));

  return true;
}

void MmService::BalloonsThreadStop() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(balloons_thread_sequence_checker_);
  balloon_broker_.reset();
  DETACH_FROM_SEQUENCE(balloons_thread_sequence_checker_);
}

void MmService::BalloonsThreadNotifyVmStarted(int vm_cid,
                                              const std::string& socket) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(balloons_thread_sequence_checker_);
  balloon_broker_->RegisterVm(vm_cid, socket);
}

void MmService::BalloonsThreadNotifyVmStopping(int vm_cid) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(balloons_thread_sequence_checker_);
  balloon_broker_->RemoveVm(vm_cid);
}

ResizePriority MmService::BalloonsThreadGetLowestBalloonBlockPriority() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(balloons_thread_sequence_checker_);
  return balloon_broker_->LowestBalloonBlockPriority();
}

void MmService::BalloonsThreadReclaim(
    const BalloonBroker::ReclaimOperation& reclaim_operation,
    ResizePriority priority) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(balloons_thread_sequence_checker_);
  balloon_broker_->Reclaim(reclaim_operation, priority);
}

}  // namespace vm_tools::concierge::mm
