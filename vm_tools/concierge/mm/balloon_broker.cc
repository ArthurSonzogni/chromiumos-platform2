// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/mm/balloon_broker.h"

#include <sys/socket.h>

// Needs to be included after sys/socket.h
#include <linux/vm_sockets.h>

#include <limits>
#include <memory>
#include <string>
#include <utility>

#include <base/logging.h>
#include <base/time/time.h>

using vm_tools::vm_memory_management::ResizePriority_Name;

namespace vm_tools::concierge::mm {

BalloonBroker::BalloonBroker(
    std::unique_ptr<KillsServer> kills_server,
    scoped_refptr<base::SequencedTaskRunner> balloon_operations_task_runner,
    BalloonBlockerFactory balloon_blocker_factory)
    : kills_server_(std::move(kills_server)),
      balloon_operations_task_runner_(balloon_operations_task_runner),
      balloon_blocker_factory_(balloon_blocker_factory) {
  kills_server_->SetClientConnectionNotification(base::BindRepeating(
      &BalloonBroker::OnNewClientConnected, base::Unretained(this)));
  kills_server_->SetClientDisconnectedNotification(base::BindRepeating(
      &BalloonBroker::OnClientDisconnected, base::Unretained(this)));
  kills_server_->SetKillRequestHandler(base::BindRepeating(
      &BalloonBroker::HandleKillRequest, base::Unretained(this)));
  kills_server_->SetNoKillCandidateNotification(base::BindRepeating(
      &BalloonBroker::HandleNoKillCandidates, base::Unretained(this)));
  kills_server_->SetDecisionLatencyNotification(base::BindRepeating(
      &BalloonBroker::HandleDecisionLatency, base::Unretained(this)));

  // Add the local context. Local context does not have a balloon.
  contexts_[VMADDR_CID_LOCAL] = {};
}

void BalloonBroker::RegisterVm(int vm_cid, const std::string& socket_path) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (contexts_.find(vm_cid) != contexts_.end()) {
    return;
  }

  kills_server_->RegisterVm(vm_cid);

  contexts_[vm_cid] = {
      .balloon = balloon_blocker_factory_(vm_cid, socket_path,
                                          balloon_operations_task_runner_),
      .clients = {}};
}

void BalloonBroker::RemoveVm(int vm_cid) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  kills_server_->RemoveVm(vm_cid);

  LOG(INFO) << "BalloonBroker removing VM. CID: " << vm_cid;
  contexts_.erase(vm_cid);
  connected_vms_.erase(vm_cid);
}

void BalloonBroker::Reclaim(const ReclaimOperation& reclaim_targets,
                            ResizePriority priority) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (connected_vms_.size() == 0) {
    return;
  }

  // Reclaiming from the host means evenly deflating all guest balloons.
  int64_t local_adjustment = 0;
  if (reclaim_targets.contains(VMADDR_CID_LOCAL)) {
    local_adjustment = -static_cast<int64_t>(
        (reclaim_targets.at(VMADDR_CID_LOCAL) / connected_vms_.size()));
  }

  for (int cid : connected_vms_) {
    int64_t resize_amount =
        reclaim_targets.contains(cid) ? reclaim_targets.at(cid) : 0;
    resize_amount += local_adjustment;
    AdjustBalloon(cid, resize_amount, priority);
  }
}

void BalloonBroker::ReclaimUntilBlocked(int vm_cid, ResizePriority priority) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // If the adjustment doesn't change the balloon size as much as requested, the
  // adjustment was blocked. Do not continue.
  if (AdjustBalloon(vm_cid, kReclaimIncrement, priority) < kReclaimIncrement) {
    return;
  }

  // Inflate again in the near future.
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&BalloonBroker::ReclaimUntilBlocked,
                     base::Unretained(this), vm_cid, priority),
      base::Seconds(1));
}

ResizePriority BalloonBroker::LowestUnblockedPriority() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  base::TimeTicks check_time = base::TimeTicks::Now();

  ResizePriority lowest_priority = ResizePriority::RESIZE_PRIORITY_UNSPECIFIED;

  for (ResizeDirection direction :
       {ResizeDirection::kInflate, ResizeDirection::kDeflate}) {
    for (const auto& context : contexts_) {
      // Local is not a VM
      if (context.first == VMADDR_CID_LOCAL) {
        continue;
      }

      ResizePriority check_priority =
          context.second.balloon->LowestUnblockedPriority(direction,
                                                          check_time);

      if (check_priority > lowest_priority) {
        lowest_priority = check_priority;
      }
    }
  }

  return lowest_priority;
}

// static
std::unique_ptr<BalloonBlocker> BalloonBroker::CreateBalloonBlocker(
    int vm_cid,
    const std::string& socket_path,
    scoped_refptr<base::SequencedTaskRunner> balloon_operations_task_runner) {
  return std::make_unique<BalloonBlocker>(
      vm_cid, std::make_unique<Balloon>(vm_cid, socket_path,
                                        balloon_operations_task_runner));
}

void BalloonBroker::OnNewClientConnected(Client client) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // Ignore invalid cids.
  if (contexts_.find(client.cid) == contexts_.end()) {
    return;
  }

  BalloonBrokerClient new_client = {
      .mm_client = client,
      .has_kill_candidates = true,
  };

  contexts_[client.cid].clients.emplace_back(std::move(new_client));

  if (client.cid != VMADDR_CID_LOCAL) {
    connected_vms_.emplace(client.cid);
  }

  LOG(INFO) << "BalloonBroker new client. CID: " << client.cid;
}

void BalloonBroker::OnClientDisconnected(Client client) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (contexts_.find(client.cid) == contexts_.end()) {
    return;
  }

  Context& context = contexts_[client.cid];

  for (size_t i = 0; i < context.clients.size(); i++) {
    if (context.clients[i].mm_client.connection_id == client.connection_id) {
      context.clients.erase(context.clients.begin() + i);
      break;
    }
  }

  if (context.clients.size() == 0) {
    contexts_.erase(client.cid);
    connected_vms_.erase(client.cid);
  }
}

size_t BalloonBroker::HandleKillRequest(Client client,
                                        size_t proc_size,
                                        ResizePriority priority) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // If a kill request is received, then the client has kill candidates.
  SetHasKillCandidates(client, true);

  base::flat_set<int> targets{client.cid};
  int64_t signed_delta = -static_cast<int64_t>(proc_size);

  if (client.cid == VMADDR_CID_LOCAL) {
    // Host requests result in an inflation of one or more of the guest(s)
    // balloon(s).
    targets = connected_vms_;
    signed_delta = std::abs(signed_delta);
  }

  int64_t balloon_delta_actual =
      EvenlyAdjustBalloons(targets, signed_delta, priority);

  // If the balloon was not adjusted as much as requested, the process should be
  // killed by the client.
  if (std::abs(balloon_delta_actual) < proc_size) {
    LOG(INFO) << "KillTrace:[" << client.cid << ","
              << ResizePriority_Name(priority) << "," << (proc_size / MiB(1))
              << "MB]";
  }

  // Track the result of this kill request.
  SetMostRecentKillRequest(client, priority, balloon_delta_actual);

  return std::abs(balloon_delta_actual);
}

void BalloonBroker::HandleNoKillCandidates(Client client) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  SetHasKillCandidates(client, false);

  if (contexts_.find(client.cid) == contexts_.end()) {
    return;
  }

  Context& context = contexts_[client.cid];

  // Check if no clients have any kill candidates.
  for (BalloonBrokerClient& connected_client : context.clients) {
    if (connected_client.has_kill_candidates) {
      // If there are still kill candidates in the vm context, then don't do
      // anything.
      return;
    }
  }

  // The context has no kill candidates and still needs to kill something,
  // give it some breathing room at a high priority.
  if (client.cid == VMADDR_CID_LOCAL) {
    EvenlyAdjustBalloons(
        connected_vms_, kNoKillCandidatesReclaimAmount,
        ResizePriority::RESIZE_PRIORITY_NO_KILL_CANDIDATES_HOST);
    return;
  }

  EvenlyAdjustBalloons(
      {client.cid}, -kNoKillCandidatesReclaimAmount,
      ResizePriority::RESIZE_PRIORITY_NO_KILL_CANDIDATES_GUEST);
}

void BalloonBroker::HandleDecisionLatency(Client client,
                                          const DecisionLatency& latency) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  BalloonBrokerClient* bb_client = GetBalloonBrokerClient(client);

  if (bb_client == nullptr) {
    return;
  }

  // If the client timed out waiting for the response but the kill request was
  // successful, this means that something was killed when it shouldn't have
  // been.
  if (latency.latency_ms() == std::numeric_limits<uint32_t>::max() &&
      bb_client->kill_request_result > 0) {
    LOG(WARNING) << "Unnecessary kill occurred for CID: " << client.cid
                 << " Priority: "
                 << ResizePriority_Name(bb_client->kill_request_priority)
                 << " Reason: Response timed out.";
  }
}

int64_t BalloonBroker::EvenlyAdjustBalloons(const base::flat_set<int>& targets,
                                            int64_t total_adjustment,
                                            ResizePriority priority) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (targets.size() == 0) {
    return 0;
  }

  const int64_t adjustment_per_vm =
      total_adjustment / static_cast<int32_t>(targets.size());

  int64_t actual_total_adjustment = 0;

  for (int target : targets) {
    actual_total_adjustment +=
        AdjustBalloon(target, adjustment_per_vm, priority);
  }

  return actual_total_adjustment;
}

int64_t BalloonBroker::AdjustBalloon(int cid,
                                     int64_t adjustment,
                                     ResizePriority priority) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!contexts_.contains(cid)) {
    return 0;
  }

  ResizeRequest request(priority, adjustment);
  return contexts_[cid].balloon->TryResize(request);
}

BalloonBroker::BalloonBrokerClient* BalloonBroker::GetBalloonBrokerClient(
    Client client) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (contexts_.find(client.cid) == contexts_.end()) {
    return nullptr;
  }

  for (BalloonBrokerClient& connected_client : contexts_[client.cid].clients) {
    if (connected_client.mm_client.connection_id == client.connection_id) {
      return &connected_client;
    }
  }

  return nullptr;
}

void BalloonBroker::SetHasKillCandidates(Client client, bool has_candidates) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  BalloonBrokerClient* bb_client = GetBalloonBrokerClient(client);

  if (bb_client) {
    bb_client->has_kill_candidates = has_candidates;
  }
}

void BalloonBroker::SetMostRecentKillRequest(Client client,
                                             ResizePriority priority,
                                             int64_t result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  BalloonBrokerClient* bb_client = GetBalloonBrokerClient(client);

  if (bb_client) {
    bb_client->kill_request_priority = priority;
    bb_client->kill_request_result = result;
  }
}

}  // namespace vm_tools::concierge::mm
