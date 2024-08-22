// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SECAGENTD_PROCESS_CACHE_H_
#define SECAGENTD_PROCESS_CACHE_H_

#include <list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "base/containers/lru_cache.h"
#include "base/files/file_path.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_refptr.h"
#include "base/synchronization/lock.h"
#include "secagentd/bpf/bpf_types.h"
#include "secagentd/device_user.h"
#include "secagentd/image_cache.h"
#include "secagentd/metrics_sender.h"
#include "secagentd/proto/security_xdr_events.pb.h"

namespace secagentd {

namespace testing {
class ProcessCacheTestFixture;
}

class ProcessCacheInterface
    : public base::RefCountedThreadSafe<ProcessCacheInterface> {
 public:
  virtual ~ProcessCacheInterface() = default;
  // Internalizes a process exec event from the BPF.
  virtual void PutFromBpfExec(const bpf::cros_process_start& process_start) = 0;
  // Evicts the given process from the cache if present.
  virtual void EraseProcess(uint64_t pid, bpf::time_ns_t start_time_ns) = 0;
  // Returns num_generations worth of processes in the process tree of the given
  // pid; including pid itself. start_time_ns is used as a safety check against
  // PID reuse.
  virtual std::vector<std::unique_ptr<cros_xdr::reporting::Process>>
  GetProcessHierarchy(uint64_t pid,
                      bpf::time_ns_t start_time_ns,
                      int num_generations) = 0;

  // Is the event a noisy background event that should be dropped?
  virtual bool IsEventFiltered(
      const cros_xdr::reporting::Process* parent_process,
      const cros_xdr::reporting::Process* process) = 0;

  // Initializes the event filter for use.
  // underscorify is used for testing and is defaulted to false. The fake root
  // in the unit test uses underscores for subdirectories rather than creating
  // real ones.
  virtual void InitializeFilter(bool underscorify = false) = 0;

  virtual void FillProcessFromBpf(
      const bpf::cros_process_task_info& task_info,
      const bpf::cros_image_info& image_info,
      cros_xdr::reporting::Process* process_proto,
      const std::list<std::string>& redacted_usernames) = 0;
};

class ProcessCache : public ProcessCacheInterface {
 public:
  struct InternalProcessKeyType {
    uint64_t start_time_t;
    uint64_t pid;
    bool operator<(const InternalProcessKeyType& rhs) const {
      return std::tie(start_time_t, pid) < std::tie(rhs.start_time_t, rhs.pid);
    }
  };
  struct InternalProcessValueType {
    std::unique_ptr<cros_xdr::reporting::Process> process_proto;
    InternalProcessKeyType parent_key;
  };
  using InternalProcessCacheType =
      base::LRUCache<InternalProcessKeyType, InternalProcessValueType>;

  struct InternalFilterRule {
    std::string image_pathname;
    // Optionally match against the entire commandline.
    std::vector<std::string> commandline;
  };

  using InternalFilterRuleSetType =
      absl::flat_hash_map<std::string, InternalFilterRule>;

  // Converts ns (from BPF) to clock_t for use in InternalProcessKeyType. It
  // would be ideal to do this conversion in the BPF but we lack the required
  // kernel constants there.
  static uint64_t LossyNsecToClockT(bpf::time_ns_t ns);

  // Converts clock_t to seconds.
  static int64_t ClockTToSeconds(uint64_t clock_t);

  static void PartiallyFillProcessFromBpfTaskInfo(
      const bpf::cros_process_task_info& task_info,
      cros_xdr::reporting::Process* process_proto,
      const std::list<std::string>& redacted_usernames);

  void FillProcessFromBpf(
      const bpf::cros_process_task_info& task_info,
      const bpf::cros_image_info& image_info,
      cros_xdr::reporting::Process* process_proto,
      const std::list<std::string>& redacted_usernames) override;

  template <typename ProtoT>
  static void FillProcessTree(
      ProtoT proto,
      const bpf::cros_process_start& process_start,
      bool has_full_process_start,
      scoped_refptr<ProcessCacheInterface> process_cache,
      scoped_refptr<DeviceUserInterface> device_user);

  explicit ProcessCache(scoped_refptr<DeviceUserInterface> device_user);
  void PutFromBpfExec(const bpf::cros_process_start& process_start) override;
  void EraseProcess(uint64_t pid, bpf::time_ns_t start_time_ns) override;
  std::vector<std::unique_ptr<cros_xdr::reporting::Process>>
  GetProcessHierarchy(uint64_t pid,
                      bpf::time_ns_t start_time_ns,
                      int num_generations) override;
  bool IsEventFiltered(const cros_xdr::reporting::Process* parent_process,
                       const cros_xdr::reporting::Process* process) override;
  void InitializeFilter(bool underscorify = false) override;

  // Allow calling the private test-only constructor without befriending
  // scoped_refptr.
  template <typename... Args>
  static scoped_refptr<ProcessCache> CreateForTesting(Args&&... args) {
    return base::WrapRefCounted(new ProcessCache(std::forward<Args>(args)...));
  }

  ProcessCache(const ProcessCache&) = delete;
  ProcessCache(ProcessCache&&) = delete;
  ProcessCache& operator=(const ProcessCache&) = delete;
  ProcessCache& operator=(ProcessCache&&) = delete;

 private:
  friend class testing::ProcessCacheTestFixture;
  // Internal constructor used for testing.
  ProcessCache(const base::FilePath& root_path,
               scoped_refptr<DeviceUserInterface> device_user,
               scoped_refptr<ImageCacheInterface> image_cache);
  // Like LRUCache::Get, returns an internal iterator to the given key. Unlike
  // LRUCache::Get, best-effort tries to fetch missing keys from procfs. Then
  // inclusively Puts them in process_cache_ if successful and returns an
  // iterator.
  std::pair<ProcessCache::InternalProcessCacheType::iterator, metrics::Cache>
  InclusiveGetProcess(const InternalProcessKeyType& key);
  absl::StatusOr<InternalProcessValueType> MakeFromProcfs(
      const InternalProcessKeyType& key);
  // Fills in the image proto from the given procfs directory. Uses
  // pid_for_setns to resolve any exe symlinks.
  absl::Status FillImageFromProcfs(
      const base::FilePath& proc_pid_dir,
      uint64_t pid_for_setns,
      cros_xdr::reporting::FileImage* file_image_proto);
  // Sends what percentage of process cache is full.
  void SendPolledMetrics();

  base::WeakPtrFactory<ProcessCache> weak_ptr_factory_;
  base::Lock process_cache_lock_;
  std::unique_ptr<InternalProcessCacheType> process_cache_;
  scoped_refptr<DeviceUserInterface> device_user_;
  const base::FilePath root_path_;
  InternalFilterRuleSetType filter_rules_parent_;
  InternalFilterRuleSetType filter_rules_process_;
  int64_t earliest_seen_exec_rel_s_;
  scoped_refptr<ImageCacheInterface> image_cache_;
};

template <typename ProtoT>
void ProcessCache::FillProcessTree(
    ProtoT proto,
    const bpf::cros_process_start& process_start,
    bool has_full_process_start,
    scoped_refptr<ProcessCacheInterface> process_cache,
    scoped_refptr<DeviceUserInterface> device_user) {
  if (has_full_process_start) {
    process_cache->FillProcessFromBpf(
        process_start.task_info, process_start.image_info,
        proto->mutable_process(), device_user->GetUsernamesForRedaction());
    auto parent_process = process_cache->GetProcessHierarchy(
        process_start.task_info.ppid, process_start.task_info.parent_start_time,
        1);
    if (parent_process.size() == 1) {
      proto->set_allocated_parent_process(parent_process[0].release());
    }
    return;
  }
  // No full process info included, fallback to using cache.
  auto hierarchy = process_cache->GetProcessHierarchy(
      process_start.task_info.pid, process_start.task_info.start_time, 2);
  if (hierarchy.empty()) {
    VLOG(1) << absl::StrFormat(
        "pid %d cmdline(%s) not in process cache. "
        "Creating a degraded %s filled with information available from BPF"
        "process map.",
        process_start.task_info.pid, process_start.task_info.commandline,
        proto->GetTypeName());
    ProcessCache::PartiallyFillProcessFromBpfTaskInfo(
        process_start.task_info, proto->mutable_process(),
        device_user->GetUsernamesForRedaction());
    auto parent_process = process_cache->GetProcessHierarchy(
        process_start.task_info.ppid, process_start.task_info.parent_start_time,
        1);
    if (parent_process.size() == 1) {
      proto->set_allocated_parent_process(parent_process[0].release());
    }
  }
  if (hierarchy.size() >= 1) {
    proto->set_allocated_process(hierarchy[0].release());
  }
  if (hierarchy.size() == 2) {
    proto->set_allocated_parent_process(hierarchy[1].release());
  }
}

}  // namespace secagentd

#endif  // SECAGENTD_PROCESS_CACHE_H_
