// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SECAGENTD_PROCESS_CACHE_H_
#define SECAGENTD_PROCESS_CACHE_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/statusor.h"
#include "base/containers/lru_cache.h"
#include "base/files/file_path.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_refptr.h"
#include "base/synchronization/lock.h"
#include "secagentd/bpf/process.h"
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

  struct InternalImageKeyType {
    uint64_t inode_device_id;
    uint64_t inode;
    bpf::cros_timespec mtime;
    bpf::cros_timespec ctime;
    bool operator<(const InternalImageKeyType& rhs) const {
      return std::tie(inode_device_id, inode, mtime.tv_sec, mtime.tv_nsec,
                      ctime.tv_sec, ctime.tv_nsec) <
             std::tie(rhs.inode_device_id, rhs.inode, rhs.mtime.tv_sec,
                      rhs.mtime.tv_nsec, rhs.ctime.tv_sec, rhs.ctime.tv_nsec);
    }
  };
  struct InternalImageValueType {
    std::string sha256;
  };
  using InternalImageCacheType =
      base::LRUCache<InternalImageKeyType, InternalImageValueType>;

  // Converts ns (from BPF) to clock_t for use in InternalProcessKeyType. It
  // would be ideal to do this conversion in the BPF but we lack the required
  // kernel constants there.
  static uint64_t LossyNsecToClockT(bpf::time_ns_t ns);

  // Converts clock_t to seconds.
  static int64_t ClockTToSeconds(uint64_t clock_t);

  static void PartiallyFillProcessFromBpfTaskInfo(
      const bpf::cros_process_task_info& task_info,
      cros_xdr::reporting::Process* process_proto);

  ProcessCache();
  void PutFromBpfExec(const bpf::cros_process_start& process_start) override;
  void EraseProcess(uint64_t pid, bpf::time_ns_t start_time_ns) override;
  std::vector<std::unique_ptr<cros_xdr::reporting::Process>>
  GetProcessHierarchy(uint64_t pid,
                      bpf::time_ns_t start_time_ns,
                      int num_generations) override;
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
  explicit ProcessCache(const base::FilePath& root_path);
  // Like LRUCache::Get, returns an internal iterator to the given key. Unlike
  // LRUCache::Get, best-effort tries to fetch missing keys from procfs. Then
  // inclusively Puts them in process_cache_ if successful and returns an
  // iterator.
  InternalProcessCacheType::const_iterator InclusiveGetProcess(
      const InternalProcessKeyType& key);
  absl::StatusOr<InternalProcessValueType> MakeFromProcfs(
      const InternalProcessKeyType& key);
  // Similar to InclusiveGetProcess but operates on image_cache_.
  // TODO(b/253661187): nsenter the process' mount namespace for correctness.
  InternalImageCacheType::const_iterator InclusiveGetImage(
      const InternalImageKeyType& image_key,
      const base::FilePath& image_path_in_ns);

  base::Lock process_cache_lock_;
  std::unique_ptr<InternalProcessCacheType> process_cache_;
  base::Lock image_cache_lock_;
  std::unique_ptr<InternalImageCacheType> image_cache_;
  const base::FilePath root_path_;
};

}  // namespace secagentd

#endif  // SECAGENTD_PROCESS_CACHE_H_
