// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "secagentd/process_cache.h"

#include <unistd.h>

#include <cinttypes>
#include <memory>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "base/containers/lru_cache.h"
#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/hash/md5.h"
#include "base/logging.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_piece_forward.h"
#include "base/strings/string_tokenizer.h"
#include "base/strings/stringprintf.h"
#include "base/synchronization/lock.h"
#include "missive/proto/security_xdr_events.pb.h"
#include "re2/re2.h"
#include "secagentd/bpf/process.h"

namespace {

namespace bpf = secagentd::bpf;
namespace pb = cros_xdr::reporting;
using secagentd::ProcessCache;

static const char kErrorFailedToStat[] = "Failed to stat ";
static const char kErrorFailedToResolve[] = "Failed to resolve ";
static const char kErrorFailedToRead[] = "Failed to read ";
static const char kErrorFailedToParse[] = "Failed to parse ";

std::string StableUuid(ProcessCache::InternalKeyType seed) {
  base::MD5Digest md5;
  base::MD5Sum(&seed, sizeof(seed), &md5);
  // Convert the hash to a UUID string. Pretend to be version 4, variant 1.
  md5.a[4] = (md5.a[4] & 0x0f) | 0x40;
  md5.a[6] = (md5.a[6] & 0x3f) | 0x80;
  return base::StringPrintf(
      "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
      md5.a[0], md5.a[1], md5.a[2], md5.a[3], md5.a[4], md5.a[5], md5.a[6],
      md5.a[7], md5.a[8], md5.a[9], md5.a[10], md5.a[11], md5.a[12], md5.a[13],
      md5.a[14], md5.a[15]);
}

// Kernel arg and env lists use '\0' to delimit elements. Tokenize the string
// and use single quotes (') to designate atomic elements.
// bufsize is the total capacity of buf (used for bounds checking).
// payload_len is the length of actual payload including the final '\0'.
std::string SafeTransformArgvEnvp(const char* buf,
                                  size_t bufsize,
                                  size_t payload_len) {
  std::string str;
  if (payload_len <= 0 || payload_len > bufsize) {
    return str;
  }
  base::CStringTokenizer t(buf, buf + payload_len, std::string("\0", 1));
  while (t.GetNext()) {
    str.append(base::StringPrintf("'%s' ", t.token().c_str()));
  }
  if (str.length() > 0) {
    str.pop_back();
  }
  return str;
}

// Fills a FileImage proto with contents from bpf image_info.
void FillImageFromBpf(const bpf::cros_image_info& image_info,
                      pb::FileImage* file_image_proto) {
  file_image_proto->set_pathname(std::string(image_info.pathname));
  file_image_proto->set_mnt_ns(image_info.mnt_ns);
  file_image_proto->set_inode_device_id(image_info.inode_device_id);
  file_image_proto->set_inode(image_info.inode);
  file_image_proto->set_canonical_uid(image_info.uid);
  file_image_proto->set_canonical_gid(image_info.gid);
  file_image_proto->set_mode(image_info.mode);
}

void FillProcessFromBpf(const bpf::cros_process_start& process_start,
                        pb::Process* process_proto) {
  ProcessCache::InternalKeyType key{process_start.start_time,
                                    process_start.pid};
  process_proto->set_process_uuid(StableUuid(key));
  process_proto->set_canonical_pid(process_start.pid);
  process_proto->set_canonical_uid(process_start.uid);
  process_proto->set_commandline(SafeTransformArgvEnvp(
      process_start.commandline, sizeof(process_start.commandline),
      process_start.commandline_len));
  FillImageFromBpf(process_start.image_info, process_proto->mutable_image());
}

absl::Status GetNsFromPath(const base::FilePath& ns_symlink_path,
                           uint64_t* ns) {
  // mnt_ns_symlink is not actually pathlike. E.g: "mnt:[4026531840]".
  constexpr char kMntNsPattern[] = R"(mnt:\[(\d+)\])";
  static const LazyRE2 kMntNsRe = {kMntNsPattern};
  base::FilePath ns_symlink;
  if (!base::ReadSymbolicLink(ns_symlink_path, &ns_symlink)) {
    return absl::NotFoundError(
        base::StrCat({kErrorFailedToResolve, ns_symlink_path.value()}));
  }
  if (!RE2::FullMatch(ns_symlink.value(), *kMntNsRe, ns)) {
    return absl::NotFoundError(
        base::StrCat({kErrorFailedToParse, ns_symlink.value()}));
  }
  return absl::OkStatus();
}

absl::Status GetStatFromProcfs(const base::FilePath& stat_path,
                               uint64_t* ppid,
                               uint64_t* starttime_t) {
  std::string proc_stat_contents;
  if (!base::ReadFileToString(stat_path, &proc_stat_contents)) {
    return absl::NotFoundError(
        base::StrCat({kErrorFailedToRead, stat_path.value()}));
  }

  // See https://man7.org/linux/man-pages/man5/proc.5.html for
  // /proc/[pid]/stat format. All tokens are delimited with a whitespace. One
  // major caveat is that comm (field 2) token may have an embedded whitespace
  // and is so delimited by parentheses. The token may also have embedded
  // parentheses though so we just ignore everything until the final ')'.
  // StringTokenizer::set_quote_chars does not help with this. It accepts
  // multiple quote chars but does not work for asymmetric quoting.
  size_t end_of_comm = proc_stat_contents.rfind(')');
  if (end_of_comm == std::string::npos) {
    return absl::OutOfRangeError(
        base::StrCat({kErrorFailedToParse, stat_path.value()}));
  }
  base::StringTokenizer t(proc_stat_contents.begin() + end_of_comm,
                          proc_stat_contents.end(), " ");
  // We could avoid a separate loop here but the tokenizer API is awkward for
  // random access.
  std::vector<base::StringPiece> stat_tokens;
  while (t.GetNext()) {
    stat_tokens.push_back(t.token_piece());
  }

  // We need the following fields (1-indexed in man page):
  // (4) ppid  %d
  // (22) starttime  %llu
  // And remember that we started tokenizing at (2) comm.
  static const size_t kPpidField = 2;
  static const size_t kStarttimeField = 20;
  if ((stat_tokens.size() <= kStarttimeField) ||
      (!base::StringToUint64(stat_tokens[kPpidField], ppid)) ||
      (!base::StringToUint64(stat_tokens[kStarttimeField], starttime_t))) {
    return absl::OutOfRangeError(
        base::StrCat({kErrorFailedToParse, stat_path.value()}));
  }
  return absl::OkStatus();
}

}  // namespace

namespace secagentd {

constexpr ProcessCache::InternalCacheType::size_type kProcessCacheMaxSize = 256;

ProcessCache::ProcessCache(const base::FilePath& root_path,
                           uint64_t sc_clock_tck)
    : cache_(std::make_unique<InternalCacheType>(kProcessCacheMaxSize)),
      root_path_(root_path),
      sc_clock_tck_(sc_clock_tck) {}

ProcessCache::ProcessCache()
    : ProcessCache(base::FilePath("/"), sysconf(_SC_CLK_TCK)) {}

void ProcessCache::PutFromBpfExec(
    const bpf::cros_process_start& process_start) {
  InternalKeyType key{LossyNsecToClockT(process_start.start_time),
                      process_start.pid};
  auto process_proto = std::make_unique<pb::Process>();
  FillProcessFromBpf(process_start, process_proto.get());
  InternalKeyType parent_key{LossyNsecToClockT(process_start.parent_start_time),
                             process_start.ppid};
  base::AutoLock lock(cache_lock_);
  cache_->Put(key, InternalValueType({std::move(process_proto), parent_key}));
}

ProcessCache::InternalCacheType::const_iterator ProcessCache::InclusiveGet(
    const InternalKeyType& key) {
  cache_lock_.AssertAcquired();
  // PID 0 doesn't exist and is also used to signify the end of the process
  // "linked list".
  if (key.pid == 0) {
    return cache_->end();
  }
  auto it = cache_->Get(key);
  if (it != cache_->end()) {
    return it;
  }

  auto statusor = MakeFromProcfs(key);
  if (!statusor.ok()) {
    LOG(ERROR) << statusor.status();
    return cache_->end();
  }

  it = cache_->Put(key, std::move(*statusor));
  return it;
}

std::vector<std::unique_ptr<pb::Process>> ProcessCache::GetProcessHierarchy(
    uint64_t pid, bpf::time_ns_t start_time_ns, int num_generations) {
  std::vector<std::unique_ptr<pb::Process>> processes;
  InternalKeyType lookup_key{LossyNsecToClockT(start_time_ns), pid};
  base::AutoLock lock(cache_lock_);
  for (int i = 0; i < num_generations; ++i) {
    auto it = InclusiveGet(lookup_key);
    if (it != cache_->end()) {
      auto process_proto = std::make_unique<pb::Process>();
      process_proto->CopyFrom(*it->second.process_proto);
      processes.push_back(std::move(process_proto));
      lookup_key = it->second.parent_key;
    } else {
      // Process no longer exists or we've reached init. Break and best-effort
      // return what we were able to retrieve.
      break;
    }
  }
  return processes;
}

absl::StatusOr<ProcessCache::InternalValueType> ProcessCache::MakeFromProcfs(
    const ProcessCache::InternalKeyType& key) const {
  InternalKeyType parent_key;
  auto process_proto = std::make_unique<pb::Process>();
  process_proto->set_canonical_pid(key.pid);
  process_proto->set_process_uuid(StableUuid(key));

  const base::FilePath proc_pid_dir =
      root_path_.Append(base::StringPrintf("proc/%" PRIu64, key.pid));
  base::stat_wrapper_t pid_dir_stat;
  if (base::File::Stat(proc_pid_dir.value().c_str(), &pid_dir_stat)) {
    return absl::NotFoundError(
        base::StrCat({kErrorFailedToStat, proc_pid_dir.value()}));
  }
  process_proto->set_canonical_uid(pid_dir_stat.st_uid);

  const base::FilePath exe_symlink_path = proc_pid_dir.Append("exe");
  base::FilePath exe_path;
  if (!base::ReadSymbolicLink(exe_symlink_path, &exe_path)) {
    return absl::NotFoundError(
        base::StrCat({kErrorFailedToResolve, exe_symlink_path.value()}));
  }
  // TODO(b/253661187): nsenter the process' mount namespace for correctness.
  base::stat_wrapper_t exe_stat;
  if (base::File::Stat(exe_path.value().c_str(), &exe_stat)) {
    return absl::NotFoundError(
        base::StrCat({kErrorFailedToStat, exe_path.value()}));
  }

  auto image_proto = process_proto->mutable_image();
  const base::FilePath mnt_ns_symlink_path =
      proc_pid_dir.Append("ns").Append("mnt");
  uint64_t mnt_ns;
  auto status = GetNsFromPath(mnt_ns_symlink_path, &mnt_ns);
  if (!status.ok()) {
    return status;
  }
  image_proto->set_pathname(exe_path.value());
  image_proto->set_mnt_ns(mnt_ns);
  image_proto->set_inode_device_id(exe_stat.st_dev);
  image_proto->set_inode(exe_stat.st_ino);
  image_proto->set_canonical_uid(exe_stat.st_uid);
  image_proto->set_canonical_gid(exe_stat.st_gid);
  image_proto->set_mode(exe_stat.st_mode);

  const base::FilePath cmdline_path = proc_pid_dir.Append("cmdline");
  std::string cmdline_contents;
  if (!base::ReadFileToString(cmdline_path, &cmdline_contents)) {
    return absl::NotFoundError(
        base::StrCat({kErrorFailedToRead, cmdline_path.value()}));
  }
  process_proto->set_commandline(
      SafeTransformArgvEnvp(cmdline_contents.c_str(), cmdline_contents.size(),
                            cmdline_contents.size()));

  // This must be the last file that we read for this process because process
  // starttime is used as a key against pid reuse.
  const base::FilePath stat_path = proc_pid_dir.Append("stat");
  uint64_t procfs_start_time_t;
  status = GetStatFromProcfs(stat_path, &parent_key.pid, &procfs_start_time_t);
  if (!status.ok()) {
    return status;
  }

  // TODO(b/254291026): Incoming ns is currently not derived using
  // timens_add_boottime_ns. So instead of checking for equality, we only
  // verify that the procfs start time is not later than what we want.
  if (key.start_time_t < procfs_start_time_t) {
    return absl::AbortedError(
        base::StringPrintf("Detected PID reuse on %" PRIu64
                           " (want time %" PRIu64 ", got time %" PRIu64 ")",
                           key.pid, key.start_time_t, procfs_start_time_t));
  }

  // parent_key.pid is filled in by this point but we also need start_time.
  // parent_key.pid == 0 implies current process is init. No need to traverse
  // further.
  if (parent_key.pid != 0) {
    const base::FilePath parent_stat_path = root_path_.Append(
        base::StringPrintf("proc/%" PRIu64 "/stat", parent_key.pid));
    uint64_t unused_ppid;
    status = GetStatFromProcfs(parent_stat_path, &unused_ppid,
                               &parent_key.start_time_t);
    if (!status.ok() || key.start_time_t < parent_key.start_time_t) {
      LOG(WARNING) << "Failed to establish parent linkage for PID " << key.pid;
      // Signifies end of our "linked list".
      parent_key.pid = 0;
    }
  }
  return InternalValueType{std::move(process_proto), parent_key};
}

uint64_t ProcessCache::LossyNsecToClockT(bpf::time_ns_t ns) const {
  static constexpr uint64_t kNsecPerSec = 1000000000;
  // Copied from the kernel procfs code though we unfortunately cannot use
  // ifdefs and need to do comparisons live.
  if ((kNsecPerSec % sc_clock_tck_) == 0) {
    return ns / (kNsecPerSec / sc_clock_tck_);
  } else if ((sc_clock_tck_ % 512) == 0) {
    return (ns * sc_clock_tck_ / 512) / (kNsecPerSec / 512);
  } else {
    return (ns * 9) /
           ((9ull * kNsecPerSec + (sc_clock_tck_ / 2)) / sc_clock_tck_);
  }
}

}  // namespace secagentd
