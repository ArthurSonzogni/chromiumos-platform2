// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_MM_BALLOON_BROKER_H_
#define VM_TOOLS_CONCIERGE_MM_BALLOON_BROKER_H_

#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <base/containers/flat_map.h>
#include <base/sequence_checker.h>
#include <base/task/sequenced_task_runner.h>
#include <metrics/metrics_library.h>

#include <vm_applications/apps.pb.h>
#include <vm_memory_management/vm_memory_management.pb.h>

#include "vm_tools/common/vm_id.h"
#include "vm_tools/concierge/byte_unit.h"
#include "vm_tools/concierge/mm/balloon_blocker.h"
#include "vm_tools/concierge/mm/balloon_metrics.h"
#include "vm_tools/concierge/mm/kills_server.h"

using vm_tools::vm_memory_management::ResizePriority;

namespace vm_tools::concierge::mm {

// The BalloonBroker is the main entrypoint into adjusting the size of
// virtio-balloons managed by the VM Memory Management Service. The
// BalloonBroker must be kept in sync with current VM lifecycle through the
// RegisterVm() and RemoveVm() functions. Callers can query the block
// state of a specific VM's balloon through the LowestUnblockedPriority()
// function and can also request to reclaim memory from a specific context
// (including the host) by using the Reclaim() function. Additionally, the
// BalloonBroker registers itself as the handler of kill decision requests and
// no kill candidate notifications that are received by the
// KillsServer.
class BalloonBroker {
 public:
  // Creates balloon instances.
  using BalloonBlockerFactory = std::function<std::unique_ptr<BalloonBlocker>(
      int,
      const std::string&,
      scoped_refptr<base::SequencedTaskRunner>,
      std::unique_ptr<BalloonMetrics>)>;

  explicit BalloonBroker(
      std::unique_ptr<KillsServer> kills_server,
      scoped_refptr<base::SequencedTaskRunner> balloon_operations_task_runner,
      const raw_ref<MetricsLibraryInterface> metrics,
      BalloonBlockerFactory balloon_blocker_factory =
          &BalloonBroker::CreateBalloonBlocker);

  BalloonBroker(const BalloonBroker&) = delete;
  BalloonBroker& operator=(const BalloonBroker&) = delete;

  // Registers a VM and the corresponding control socket with the broker.
  void RegisterVm(apps::VmType vm_type,
                  int vm_cid,
                  const std::string& socket_path);

  // Removes a VM and its corresponding balloon from the broker.
  void RemoveVm(int vm_cid);

  // Returns the lowest ResizePriority among all balloons that will not be
  // blocked. If all balloons are blocked at the highest priority,
  // RESIZE_PRIORITY_UNSPECIFIED is returned.
  ResizePriority LowestUnblockedPriority() const;

  // A reclaim operation consists of reclaim from one or more contexts. This can
  // be represented as a set mapping a CID to a number of bytes to reclaim.
  using ReclaimOperation = base::flat_map<int, size_t>;
  // Performs the specified reclaim operations at |priority|.
  void Reclaim(const ReclaimOperation& reclaim_targets,
               ResizePriority priority);

  // Callback for when ReclaimUntilBlocked() completes.
  using ReclaimUntilBlockedCallback =
      base::OnceCallback<void(bool success, const char* err_msg)>;
  // Reclaim all memory from |vm_cid| that is not needed with priority at least
  // |priority|.
  void ReclaimUntilBlocked(int vm_cid,
                           ResizePriority priority,
                           ReclaimUntilBlockedCallback cb);

  // Stops the ongoing ReclaimUntilBlocked() operation.
  void StopReclaimUntilBlocked(int vm_cid);

 private:
  // Contains state related to a client that is connected to the VM memory
  // management service (i.e. resourced, ARCVM's LMKD).
  struct BalloonBrokerClient {
    // The corresponding client from the server.
    Client mm_client;

    // Whether this client currently has kill candidates.
    bool has_kill_candidates = true;

    // The priority of the most recent kill request from this client.
    ResizePriority kill_request_priority =
        ResizePriority::RESIZE_PRIORITY_UNSPECIFIED;

    // The result of the most recent kill request from this client.
    int64_t kill_request_result = 0;
  };

  // Contains state related to a specific context (i.e. host, ARCVM).
  struct Context {
    // The balloon blocker instance for this context (remains null for the
    // host's context).
    std::unique_ptr<BalloonBlocker> balloon;

    // All of the clients that have connected from this context.
    // TODO(b:307477987) Originally both Ash and Lacros were separate clients on
    // the host and thus the BalloonBroker needed to support multiple clients
    // from one context. Since this is no longer the case, this logic can be
    // simplified to only have one client from each context.
    std::vector<BalloonBrokerClient> clients;
  };

  // The amount to adjust the balloon if there are no kill candidates in a
  // context, but it is facing persistent memory pressure.
  //
  // This is purposefully large so that in the case of high host memory pressure
  // with low guest memory pressure the balloon inflates quickly.
  static constexpr int64_t kNoKillCandidatesReclaimAmount = MiB(128);

  // Creates a balloon.
  static std::unique_ptr<BalloonBlocker> CreateBalloonBlocker(
      int vm_cid,
      const std::string& socket_path,
      scoped_refptr<base::SequencedTaskRunner> balloon_operations_task_runner,
      std::unique_ptr<BalloonMetrics> metrics);

  // START: Server Callbacks.

  // Callback to be run when a new client is connected to the VM memory
  // management service.
  void OnNewClientConnected(Client client);

  // Callback to be run when a client disconnects from the VM memory management
  // service.
  void OnClientDisconnected(Client client);

  // Callback to be run when a client requests a kill decision.
  size_t HandleKillRequest(Client client,
                           size_t proc_size,
                           ResizePriority priority);

  // Callback to be run when a client has no kill candidates.
  void HandleNoKillCandidates(Client client);

  // Callback to be run when a decision latency packet is received.
  void HandleDecisionLatency(Client client, const DecisionLatency& latency);

  // END: Server Callbacks.

  // Attempts to evenly adjust the target balloons at the target priority.
  // Returns the actual total adjustment.
  int64_t EvenlyAdjustBalloons(const base::flat_set<int>& targets,
                               int64_t total_adjustment,
                               ResizePriority priority);

  // Adjusts the balloon for |cid| by |adjustment| at |priority|. Returns the
  // actual balloon delta in bytes.
  int64_t AdjustBalloon(int cid, int64_t adjustment, ResizePriority priority);

  // Returns the BalloonBrokerClient that corresponds to |client|.
  BalloonBrokerClient* GetBalloonBrokerClient(Client client);

  std::string GetMetricName(int cid,
                            const std::string& unprefixed_metric_name) const;

  // Sets the kill candidate state for the specified client.
  void SetHasKillCandidates(Client client, bool has_candidates);

  // Sets the kill request result for the client.
  void SetMostRecentKillRequest(Client client,
                                ResizePriority priority,
                                int64_t result);

  // Performs one balloon adjustment step, as part of the overall
  // ReclaimUntilBlocked() process.
  void ReclaimUntilBlockedStep();

  // Constants for determining how fast ReclaimUntilBlocked() operates.
  static constexpr int64_t kReclaimTargetPerSecond = MiB(200);
  static constexpr int64_t kReclaimStepsPerSecond = 5;
  static constexpr int64_t kReclaimIncrement =
      kReclaimTargetPerSecond / kReclaimStepsPerSecond;

  // The server that listens for and handles kills related messages.
  const std::unique_ptr<KillsServer> kills_server_;

  // The task runner on which to run balloon operations.
  const scoped_refptr<base::SequencedTaskRunner>
      balloon_operations_task_runner_;

  // Creates balloon instances.
  const BalloonBlockerFactory balloon_blocker_factory_;

  // Ensure calls are made on the right sequence.
  SEQUENCE_CHECKER(sequence_checker_);

  // Maps from a CID (int) to a Context state.
  base::flat_map<int, Context> contexts_
      GUARDED_BY_CONTEXT(sequence_checker_){};

  // Maintains the list of VMs that are currently connected.
  base::flat_set<int> connected_vms_ GUARDED_BY_CONTEXT(sequence_checker_){};

  // Parameters used for the current ReclaimUntilBlocked() operation.
  std::optional<std::pair<int /* cid */, ResizePriority>>
      reclaim_until_blocked_params_ GUARDED_BY_CONTEXT(sequence_checker_);

  // Callbacks to be invoked when the current ReclaimUntilBlocked() completes.
  //
  // Although we don't expect multiple overlapping reclaim requests in the real
  // world, certain vmm-swap tests can trigger the aggressive balloon while
  // post boot reclaim is still ongoing. It's simpler to support overlapping
  // calls here than to expose enough information to support coordination at
  // a higher level.
  std::deque<ReclaimUntilBlockedCallback> reclaim_until_blocked_cbs_
      GUARDED_BY_CONTEXT(sequence_checker_);

  // Used for logging metrics related to balloon events.
  const raw_ref<MetricsLibraryInterface> metrics_
      GUARDED_BY_CONTEXT(sequence_checker_);
};

}  // namespace vm_tools::concierge::mm

#endif  // VM_TOOLS_CONCIERGE_MM_BALLOON_BROKER_H_
