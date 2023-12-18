// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_MM_RECLAIM_BROKER_H_
#define VM_TOOLS_CONCIERGE_MM_RECLAIM_BROKER_H_

#include <memory>
#include <string>
#include <vector>

#include <base/containers/flat_set.h>
#include <base/files/file_path.h>
#include <base/files/scoped_file.h>
#include <base/sequence_checker.h>

#include <vm_memory_management/vm_memory_management.pb.h>

#include "vm_tools/concierge/byte_unit.h"
#include "vm_tools/concierge/mm/balloon_broker.h"
#include "vm_tools/concierge/mm/reclaim_server.h"
#include "vm_tools/concierge/sysfs_notify_watcher.h"

using vm_tools::vm_memory_management::MglruStats;
using vm_tools::vm_memory_management::ResizePriority;

namespace vm_tools::concierge::mm {

// The ReclaimBroker receives MGLRU stats updates from VMs and the host and
// performs reclaim operations based on the distribution of the MGLRU
// generations. By default it attempts to balance the average age of MGLRU cache
// among the host and all VMs. Note: for more information about MGLRU see:
// https://docs.kernel.org/admin-guide/mm/multigen_lru.html The basic reclaim
// algorithm is this: When a new generation is created in any context: Find the
// ages of the oldest generations for each context. Choose the youngest of all
// of these oldest generations. Call the age of this generation T. In every
// context, calculate how much of the page cache is older than T. Reclaim that
// number of bytes from the context.
class ReclaimBroker {
 public:
  using LowestUnblockedPriorityCallback =
      base::RepeatingCallback<ResizePriority(void)>;
  using ReclaimCallback = base::RepeatingCallback<void(
      const BalloonBroker::ReclaimOperation&, ResizePriority)>;

  struct Config {
    base::FilePath mglru_path;
    std::unique_ptr<ReclaimServer> reclaim_server;
    LowestUnblockedPriorityCallback lowest_unblocked_priority;
    ReclaimCallback reclaim_handler;
    size_t reclaim_threshold = kDefaultReclaimThreshold;
  };

  static std::unique_ptr<ReclaimBroker> Create(Config config);

  ReclaimBroker(const ReclaimBroker&) = delete;
  ReclaimBroker& operator=(const ReclaimBroker&) = delete;

  // Registers a VM that will be managed by the ReclaimBroker
  void RegisterVm(int vm_cid);

  // Removes a VM context from the reclaim broker.
  void RemoveVm(int vm_cid);

 private:
  ReclaimBroker(Config config,
                std::unique_ptr<SysfsNotifyWatcher> mglru_watcher,
                base::ScopedFD watched_mglru_fd);

  // Callbacks for memory management server.
  void OnClientConnected(Client client);
  void OnKillRequest(Client client);
  void NewGenerationEvent(int cid, MglruStats stats);
  void HandleNoKillCandidates(Client client);

  // Callback for sysfs notify watcher.
  void OnNewLocalMglruGeneration(bool success);

  // Registers a new VM context to be managed by the reclaim broker.
  void RegisterNewContext(int cid);

  // Gets the MGLRU stats for the specified VM CID.
  std::optional<MglruStats> GetMglruStats(int cid);

  // Gets the local MGLRU stats from the mglru path.
  std::optional<MglruStats> GetLocalMglruStats();

  // Returns true iff |stats| are valid MGLRU stats.
  bool StatsAreValid(const MglruStats& stats);

  // The watcher that is watching the opened MGLRU admin file.
  const std::unique_ptr<SysfsNotifyWatcher> mglru_watcher_;

  // The fd of the opened MGLRU admin file.
  // Note: the open fd must be destructed before the SysfsNotifyWatcher so any
  // poll() calls are interrupted by the fd being closed.
  const base::ScopedFD watched_mglru_fd_;

  // The server that listens for stats related messages.
  const std::unique_ptr<ReclaimServer> reclaim_server_;

  // Callback to retrieve the lowest block priority of any balloon.
  const LowestUnblockedPriorityCallback lowest_unblocked_priority_;

  // Callback to execute reclaim operations.
  const ReclaimCallback reclaim_handler_;

  // The default reclaim threshold. Reclaim operations under this amount will be
  // ignored.
  static constexpr size_t kDefaultReclaimThreshold = MiB(1);
  // Do not reclaim for amounts under this size.
  const size_t reclaim_threshold_;

  // Ensure calls are made on the right sequence.
  SEQUENCE_CHECKER(sequence_checker_);

  // Contains the set of contexts managed by the reclaim broker.
  base::flat_set<int> contexts_ GUARDED_BY_CONTEXT(sequence_checker_){};

  static constexpr base::TimeDelta kReclaimInterval = base::Seconds(30);
  base::TimeTicks last_reclaim_event_time_
      GUARDED_BY_CONTEXT(sequence_checker_){};
};

}  // namespace vm_tools::concierge::mm

#endif  // VM_TOOLS_CONCIERGE_MM_RECLAIM_BROKER_H_
