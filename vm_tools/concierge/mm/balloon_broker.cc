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
#include <base/strings/strcat.h>
#include <base/time/time.h>

#include <vm_applications/apps.pb.h>
#include <vm_memory_management/vm_memory_management.pb.h>

using vm_tools::vm_memory_management::ResizePriority_Name;

namespace vm_tools::concierge::mm {

namespace {

// Metrics definitions

// The prefix for all Virtual Machine Memory Management Service metrics.
constexpr char kMetricsPrefix[] = "Memory.VMMMS.";

// DecisionLatency tracks how much time the VMMMS adds to clients when they are
// deciding what to kill under memory pressure. It is very important that this
// number is never very high, even at p99.
constexpr char kDecisionLatencyMetric[] = ".DecisionLatency";
constexpr base::TimeDelta kDecisionLatencyMetricMin = base::Seconds(0);
constexpr base::TimeDelta kDecisionLatencyMetricMax = base::Seconds(5);
constexpr size_t kDecisionLatencyMetricBuckets = 100;

// DecisionTimeout tracks how often we cause clients to time out. This should
// never happen, so we will use UMA to verify.
constexpr char kDecisionTimeoutMetric[] = ".DecisionTimeout";

// UnnecessaryKill tracks how often a timeout caused a client to kill something
// unnecessarily. This tracks the user-impact of timeouts, and should help us
// diagnose engagement regressions cuased by latency in VMMMS.
constexpr char kUnnecessaryKillMetric[] = ".UnnecessaryKill";

}  // namespace

BalloonBroker::BalloonBroker(
    std::unique_ptr<KillsServer> kills_server,
    scoped_refptr<base::SequencedTaskRunner> balloon_operations_task_runner,
    const raw_ref<MetricsLibraryInterface> metrics,
    BalloonBlockerFactory balloon_blocker_factory)
    : kills_server_(std::move(kills_server)),
      balloon_operations_task_runner_(balloon_operations_task_runner),
      balloon_blocker_factory_(balloon_blocker_factory),
      metrics_(metrics) {
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

void BalloonBroker::RegisterVm(apps::VmType vm_type,
                               int vm_cid,
                               const std::string& socket_path) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (contexts_.find(vm_cid) != contexts_.end()) {
    return;
  }

  kills_server_->RegisterVm(vm_cid);

  contexts_[vm_cid] = {.balloon = balloon_blocker_factory_(
                           vm_cid, socket_path, balloon_operations_task_runner_,
                           std::make_unique<BalloonMetrics>(vm_type, metrics_)),
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

  // First check to see if there is a current reclaim until operation at the
  // lowest priority. If there is, it should be cancelled when a new reclaim
  // operation is started.
  //
  // By handling low priority reclaim operations here instead of as a block in
  // the BalloonBlocker, the reclaim operation that cancels the
  // ReclaimUntilBlocked() will still be granted and resize the balloon
  // appropriately.
  if (reclaim_until_blocked_params_ &&
      reclaim_until_blocked_params_->second ==
          ResizePriority::RESIZE_PRIORITY_MGLRU_RECLAIM) {
    StopReclaimUntilBlocked(reclaim_until_blocked_params_->first);
  }

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

void BalloonBroker::ReclaimUntilBlocked(int vm_cid,
                                        ResizePriority priority,
                                        ReclaimUntilBlockedCallback cb) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (reclaim_until_blocked_params_) {
    int cur_cid = reclaim_until_blocked_params_->first;
    if (cur_cid != vm_cid) {
      LOG(ERROR) << "Already reclaiming " << cur_cid << ", can't reclaim "
                 << vm_cid;
      std::move(cb).Run(false, "already reclaiming");
      return;
    }

    reclaim_until_blocked_cbs_.emplace_back(std::move(cb));

    // If the request is at a lower priority than the ongoing operation, then
    // the current operation will fulfil the new request. Otherwise we need to
    // bump up the priority of the ongoing request. Note that it's possible for
    // a deflate request with priority below this reclaim operation to be
    // granted before the next ReclaimUntilBlockedStep(). However, that cannot
    // be differentiated from the deflate request occurring immediately before
    // the start of this reclaim operation, so it is a benign race.
    if (reclaim_until_blocked_params_->second > priority) {
      reclaim_until_blocked_params_.emplace(vm_cid, priority);
    }
  } else {
    // ReclaimUntilBlocked can spam BalloonTrace logs, so disable logging when
    // reclaiming at a low priority and then re-enable them when the reclaim
    // operation is complete.
    if (priority == ResizePriority::RESIZE_PRIORITY_LOWEST) {
      SetShouldLogBalloonTrace(vm_cid, false);

      // Unretained(this) is safe because the callback is owned by this
      // instance.
      reclaim_until_blocked_cbs_.emplace_back(
          base::BindOnce(&BalloonBroker::SetShouldLogBalloonTraceAsCallback,
                         base::Unretained(this), vm_cid, true));
    }

    reclaim_until_blocked_params_.emplace(vm_cid, priority);
    reclaim_until_blocked_cbs_.emplace_back(std::move(cb));
    ReclaimUntilBlockedStep();
  }
}

void BalloonBroker::ReclaimUntilBlockedStep() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!reclaim_until_blocked_params_) {
    return;
  }

  int vm_cid = reclaim_until_blocked_params_->first;
  ResizePriority priority = reclaim_until_blocked_params_->second;

  // If the adjustment doesn't change the balloon size as much as requested, the
  // adjustment was blocked. Do not continue.
  if (AdjustBalloon(vm_cid, kReclaimIncrement, priority) < kReclaimIncrement) {
    while (!reclaim_until_blocked_cbs_.empty()) {
      std::move(reclaim_until_blocked_cbs_.front()).Run(true, "");
      reclaim_until_blocked_cbs_.pop_front();
    }
    reclaim_until_blocked_params_.reset();
    return;
  }

  // Inflate again in the near future.
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&BalloonBroker::ReclaimUntilBlockedStep,
                     base::Unretained(this)),
      base::Seconds(1) / kReclaimStepsPerSecond);
}

void BalloonBroker::StopReclaimUntilBlocked(int vm_cid) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!reclaim_until_blocked_params_) {
    LOG(WARNING) << "StopReclaimUntilBlocked while operation not ongoing";
    return;
  }

  if (reclaim_until_blocked_params_->first != vm_cid) {
    LOG(WARNING) << "StopReclaimUntilBlocked for different target "
                 << reclaim_until_blocked_params_->first << " vs " << vm_cid;
    return;
  }

  while (!reclaim_until_blocked_cbs_.empty()) {
    std::move(reclaim_until_blocked_cbs_.front())
        .Run(false, "reclaim all cancelled");
    reclaim_until_blocked_cbs_.pop_front();
  }

  reclaim_until_blocked_params_.reset();
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
    scoped_refptr<base::SequencedTaskRunner> balloon_operations_task_runner,
    std::unique_ptr<BalloonMetrics> metrics) {
  return std::make_unique<BalloonBlocker>(
      vm_cid,
      std::make_unique<Balloon>(vm_cid, socket_path,
                                balloon_operations_task_runner),
      std::move(metrics));
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

  if (latency.latency_ms() < std::numeric_limits<uint32_t>::max()) {
    // Not a timeout, log the latency.
    metrics_->SendTimeToUMA(
        GetMetricName(client.cid, kDecisionLatencyMetric),
        base::Milliseconds(latency.latency_ms()), kDecisionLatencyMetricMin,
        kDecisionLatencyMetricMax, kDecisionLatencyMetricBuckets);
  } else {
    // Timeout, log the priority of the failed request.
    metrics_->SendEnumToUMA(GetMetricName(client.cid, kDecisionTimeoutMetric),
                            static_cast<int>(bb_client->kill_request_priority),
                            ResizePriority::RESIZE_PRIORITY_N_PRIORITIES);
    if (bb_client->kill_request_result > 0) {
      // If the client timed out waiting for the response but the kill request
      // was successful, this means that something was killed when it shouldn't
      // have been.
      LOG(WARNING) << "Unnecessary kill occurred for CID: " << client.cid
                   << " Priority: "
                   << ResizePriority_Name(bb_client->kill_request_priority)
                   << " Reason: Response timed out.";
      metrics_->SendEnumToUMA(
          GetMetricName(client.cid, kUnnecessaryKillMetric),
          static_cast<int>(bb_client->kill_request_priority),
          ResizePriority::RESIZE_PRIORITY_N_PRIORITIES);
    }
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

std::string BalloonBroker::GetMetricName(
    int cid, const std::string& unprefixed_metric_name) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (cid == VMADDR_CID_LOCAL) {
    return base::StrCat({kMetricsPrefix, "Host", unprefixed_metric_name});
  }

  auto find = contexts_.find(cid);
  if (find == contexts_.end()) {
    return base::StrCat({kMetricsPrefix, "Unknown", unprefixed_metric_name});
  }

  const apps::VmType vm_type = find->second.balloon->GetVmType();
  return base::StrCat(
      {kMetricsPrefix, apps::VmType_Name(vm_type), unprefixed_metric_name});
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

void BalloonBroker::SetShouldLogBalloonTrace(int cid, bool do_log) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!contexts_.contains(cid) || !contexts_[cid].balloon) {
    LOG(WARNING) << "Cannot set balloon trace state for non-existant context: "
                 << cid;
    return;
  }

  contexts_[cid].balloon->SetShouldLogBalloonTrace(do_log);
}

void BalloonBroker::SetShouldLogBalloonTraceAsCallback(int cid,
                                                       bool do_log,
                                                       bool,
                                                       const char*) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return SetShouldLogBalloonTrace(cid, do_log);
}

}  // namespace vm_tools::concierge::mm
