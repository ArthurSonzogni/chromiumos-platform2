// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/mm/reclaim_broker.h"

#include <fcntl.h>
#include <sys/socket.h>

// Needs to be included after sys/socket.h
#include <linux/vm_sockets.h>

#include <limits>
#include <memory>
#include <tuple>
#include <utility>

#include <base/containers/flat_map.h>
#include <base/logging.h>
#include <base/files/file_util.h>
#include <base/memory/ptr_util.h>
#include <base/posix/eintr_wrapper.h>

#include "vm_tools/concierge/mm/mglru.h"
#include "vm_tools/concierge/mm/reclaim_server.h"

using vm_tools::vm_memory_management::MglruGeneration;
using vm_tools::vm_memory_management::MglruMemcg;
using vm_tools::vm_memory_management::MglruNode;
using vm_tools::vm_memory_management::ResizePriority;

namespace vm_tools::concierge::mm {
namespace {

struct MglruGenerationInternal {
  // The id of the memcg to which this generation belongs.
  uint32_t cg_id;
  // The id of the node to which this generation belongs.
  uint32_t node_id;
  // The generation itself.
  MglruGeneration generation;
};

// Iterates through every generation in |stats| and performs |op| on the
// generations. We don't expect more than ~3 total generations per context and
// the number of contexts is equal to the number of VMs + 1 (host), so this
// function will not do much iteration even though it is O(N^3).
void ForEachGeneration(const MglruStats& stats,
                       std::function<void(const MglruGenerationInternal&)> op) {
  for (const MglruMemcg& cg : stats.cgs()) {
    for (const MglruNode& node : cg.nodes()) {
      for (const MglruGeneration& generation : node.generations()) {
        op({cg.id(), node.id(), generation});
      }
    }
  }
}

// Returns the timestamp corresponding to the generation that is the oldest
// within one context (i.e. host, ARCVM) but the youngest among all the oldest
// generations. For example given the following generation ages where a larger
// value corresponds to an older generation: Host: 5, 10, 15 ARCVM: 6, 7, 8
// Other VM: 1, 5, 10
// This function will return 8 since it is the oldest generation within ARCVM,
// but the younger than the oldest generations in the other two contexts.
uint32_t GetNewestOldestGenAge(
    const base::flat_map<int, MglruStats>& stats_map) {
  uint32_t newest_oldest = std::numeric_limits<uint32_t>::max();

  for (const auto& context : stats_map) {
    // Oldest generation will have the largest timestamp (age).
    uint32_t contexts_oldest = 0;

    // This should find the generation among all cgs and nodes that is the
    // oldest in this context. The cg and node ids can safely be ignored.
    ForEachGeneration(context.second, [&](const MglruGenerationInternal& gen) {
      if (gen.generation.timestamp_msec() > contexts_oldest) {
        contexts_oldest = gen.generation.timestamp_msec();
      }
    });

    if (contexts_oldest < newest_oldest) {
      newest_oldest = contexts_oldest;
    }
  }

  return newest_oldest;
}

// Calculates and returns the number of KiB in the specified generation that are
// older than the target age. Assumes that all bytes within the generation are
// evenly distributed in age. |next_gen_age| is necessary to know if all, some,
// or none of the specified generation is older than the target. If there is no
// next generation, 0 should be passed for next_gen_age.
uint32_t KbInGenOlderThan(const MglruGeneration& gen,
                          uint32_t target_age,
                          uint32_t next_gen_age,
                          bool include_anon) {
  uint32_t gen_age = gen.timestamp_msec();

  // If this generation is younger, then nothing can be older
  // than |target_age|.
  if (gen_age <= target_age) {
    return 0;
  }

  uint32_t total_kb = (gen.file_kb() + (include_anon ? gen.anon_kb() : 0));

  // If this generation is older and the next generation is also older,
  // then everything in this generation is older.
  if (next_gen_age > target_age) {
    return total_kb;
  }

  // If this generation is older and the next generation is younger (or
  // doesn't exist), then a portion of this generation is older.
  uint32_t gen_duration = gen_age - next_gen_age;
  uint32_t duration_older = gen_age - target_age;

  // Safety check to avoid divide by 0. The generation duration should never be
  // 0, so this is a safeguard against untrusted input from clients.
  if (gen_duration == 0) {
    return 0;
  }

  return (total_kb * duration_older) / gen_duration;
}

// Calculates and returns the number of KiB in |stats| older than |target_age|.
// Assumes that all bytes within a generation are evenly distributed in time
// within a generation.
uint32_t KbOlderThan(const MglruStats& stats,
                     uint32_t target_age,
                     bool include_anon) {
  uint32_t count = 0;
  std::optional<MglruGenerationInternal> previous_generation = std::nullopt;

  ForEachGeneration(stats, [&](const MglruGenerationInternal& gen) {
    if (previous_generation) {
      uint32_t next_gen_age = gen.generation.timestamp_msec();

      // If the previous generation does not belong to the same cg or node
      // as this one, do not use the current one in calculating the
      // proportion of the previous generation older than target_age.
      if (previous_generation->cg_id != gen.cg_id ||
          previous_generation->node_id != gen.node_id) {
        next_gen_age = 0;
      }

      // Calculate the proportion of the previous gen that should be
      // included.
      count += KbInGenOlderThan(previous_generation->generation, target_age,
                                next_gen_age, include_anon);
    }

    previous_generation = gen;
  });

  // Count the last generation in the list.
  if (previous_generation) {
    count += KbInGenOlderThan(previous_generation->generation, target_age, 0,
                              include_anon);
  }

  return count;
}

std::string SeekReadMglruAdminFile(const base::ScopedFD& file) {
  lseek(file.get(), 0, SEEK_SET);

  // The admin file is a sysfs file and therefore will be at most |page_size| in
  // length.
  std::string file_contents(getpagesize(), 0);

  size_t size = read(file.get(), file_contents.data(), file_contents.length());

  if (size < 0) {
    PLOG(ERROR) << "Failed to read from MGLRU file";
    return {};
  }

  file_contents.resize(size);
  return file_contents;
}

}  // namespace

// static
std::unique_ptr<ReclaimBroker> ReclaimBroker::Create(Config config) {
  base::ScopedFD mglru_fd;
  mglru_fd.reset(HANDLE_EINTR(
      open(config.mglru_path.value().c_str(), O_RDONLY | O_CLOEXEC)));

  if (!mglru_fd.is_valid()) {
    LOG(ERROR) << "Failed to open MGLRU admin file.";
    return {};
  }

  std::unique_ptr<SysfsNotifyWatcher> watcher =
      SysfsNotifyWatcher::Create(mglru_fd.get(), {});

  if (!watcher) {
    LOG(ERROR) << "Failed to start watching MGLRU file.";
    return {};
  }

  return base::WrapUnique(new ReclaimBroker(
      std::move(config), std::move(watcher), std::move(mglru_fd)));
}

ReclaimBroker::ReclaimBroker(Config config,
                             std::unique_ptr<SysfsNotifyWatcher> mglru_watcher,
                             base::ScopedFD watched_mglru_fd)
    : mglru_watcher_(std::move(mglru_watcher)),
      watched_mglru_fd_(std::move(watched_mglru_fd)),
      reclaim_server_(std::move(config.reclaim_server)),
      lowest_unblocked_priority_(config.lowest_unblocked_priority),
      reclaim_handler_(config.reclaim_handler),
      reclaim_threshold_(config.reclaim_threshold) {
  mglru_watcher_->SetCallback(base::BindRepeating(
      &ReclaimBroker::OnNewLocalMglruGeneration, base::Unretained(this)));

  reclaim_server_->SetClientConnectionNotification(base::BindRepeating(
      &ReclaimBroker::OnClientConnected, base::Unretained(this)));
  reclaim_server_->SetNewGenerationNotification(base::BindRepeating(
      &ReclaimBroker::NewGenerationEvent, base::Unretained(this)));

  // Always monitor the local context.
  RegisterNewContext(VMADDR_CID_LOCAL);
}

void ReclaimBroker::RegisterVm(int vm_cid) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // For the reclaim broker's state, VMs are only added to contexts_ once a
  // client has connected from that CID. Don't add a new context here.

  reclaim_server_->RegisterVm(vm_cid);
}

void ReclaimBroker::RemoveVm(int vm_cid) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  reclaim_server_->RemoveVm(vm_cid);
  contexts_.erase(vm_cid);
}

void ReclaimBroker::OnClientConnected(Client client) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  RegisterNewContext(client.cid);
}

void ReclaimBroker::NewGenerationEvent(int cid, MglruStats new_stats) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // Do not reclaim more than once within a reclaim interval.
  if (base::TimeTicks::Now() - last_reclaim_event_time_ < kReclaimInterval) {
    return;
  }

  last_reclaim_event_time_ = base::TimeTicks::Now();

  if (contexts_.find(cid) == contexts_.end()) {
    LOG(ERROR) << "Received new generation for invalid VM Context";
    return;
  }

  // If the lowest block priority is higher than the reclaim priority, there is
  // nothing to do. Note that a higher priority has a lower numerical value.
  if (lowest_unblocked_priority_.Run() <
      ResizePriority::RESIZE_PRIORITY_MGLRU_RECLAIM) {
    return;
  }

  // Retrieve MGLRU stats for all VM contexts.
  base::flat_map<int, MglruStats> stats_map{};
  for (int context : contexts_) {
    std::optional<MglruStats> stats;

    if (cid == context) {
      // Use the file contents we received.
      stats = new_stats;
    } else {
      // Request the file contents.
      stats = GetMglruStats(context);
    }

    if (!stats || !StatsAreValid(*stats)) {
      LOG(ERROR) << "Failed to retrieve MGLRU stats for CID: " << context;
      continue;
    }

    stats_map[context] = *stats;
  }

  // Perform the reclaim algorithm as described in the class header.
  uint32_t newest_oldest_gen = GetNewestOldestGenAge(stats_map);

  BalloonBroker::ReclaimOperation reclaim_operation;

  for (const auto& stats : stats_map) {
    size_t bytes_to_reclaim =
        KiB(KbOlderThan(stats.second, newest_oldest_gen, false));

    if (bytes_to_reclaim <= reclaim_threshold_) {
      // Don't bother with reclaim if it's below the reclaim threshold.
      continue;
    }

    reclaim_operation[stats.first] = bytes_to_reclaim;
  }

  if (reclaim_operation.size() > 0) {
    reclaim_handler_.Run(reclaim_operation,
                         ResizePriority::RESIZE_PRIORITY_MGLRU_RECLAIM);
  }

  return;
}

void ReclaimBroker::OnNewLocalMglruGeneration(bool success) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!success) {
    return;
  }

  std::optional<MglruStats> stats = mglru::ParseStatsFromString(
      SeekReadMglruAdminFile(watched_mglru_fd_), getpagesize());

  if (!stats) {
    return;
  }

  NewGenerationEvent(VMADDR_CID_LOCAL, *stats);
}

void ReclaimBroker::RegisterNewContext(int cid) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (contexts_.find(cid) == contexts_.end()) {
    LOG(INFO) << "ReclaimBroker new Context: " << cid;
    contexts_.emplace(cid);
  }
}

std::optional<MglruStats> ReclaimBroker::GetMglruStats(int cid) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (cid == VMADDR_CID_LOCAL) {
    return GetLocalMglruStats();
  }

  return reclaim_server_->GetMglruStats(cid);
}

std::optional<MglruStats> ReclaimBroker::GetLocalMglruStats() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return mglru::ParseStatsFromString(SeekReadMglruAdminFile(watched_mglru_fd_),
                                     getpagesize());
}

bool ReclaimBroker::StatsAreValid(const MglruStats& stats) {
  // MGLRU stats can originate from the clients and should not be trusted.
  // Perform a brief sanity check to ensure they are not too large.
  static constexpr size_t kMaxCgsCount = 5;
  static constexpr size_t kMaxNodeCount = 5;
  static constexpr size_t kMaxGenerationCount = 10;

  if (stats.cgs_size() > kMaxCgsCount) {
    return false;
  }

  for (const MglruMemcg& cg : stats.cgs()) {
    if (cg.nodes_size() > kMaxNodeCount) {
      return false;
    }

    for (const MglruNode& node : cg.nodes()) {
      if (node.generations_size() > kMaxGenerationCount) {
        return false;
      }
    }
  }

  return true;
}

}  // namespace vm_tools::concierge::mm
