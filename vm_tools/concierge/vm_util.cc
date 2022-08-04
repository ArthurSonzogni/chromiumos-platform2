// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/vm_util.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>

#include <base/base64.h>
#include <base/containers/cxx20_erase.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/format_macros.h>
#include <base/json/json_reader.h>
#include <base/logging.h>
#include <base/posix/eintr_wrapper.h>
#include <base/strings/safe_sprintf.h>
#include <base/strings/strcat.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/system/sys_info.h>
#include <brillo/files/file_util.h>
#include <brillo/process/process.h>

namespace vm_tools {
namespace concierge {

const char kCrosvmBin[] = "/usr/bin/crosvm";

namespace {

// Uid and gid mappings for the android data directory. This is a
// comma-separated list of 3 values: <start of range inside the user namespace>
// <start of range outside the user namespace> <count>. The values are taken
// from platform2/arc/container-bundle/pi/config.json.
constexpr char kAndroidUidMap[] =
    "0 655360 5000,5000 600 50,5050 660410 1994950";
constexpr char kAndroidGidMap[] =
    "0 655360 1065,1065 20119 1,1066 656426 3934,5000 600 50,5050 660410 "
    "1994950";

constexpr char kFontsSharedDir[] = "/usr/share/fonts";
constexpr char kFontsSharedDirTag[] = "fonts";

std::string BooleanParameter(const char* parameter, bool value) {
  std::string result = base::StrCat({parameter, value ? "true" : "false"});
  return result;
}

}  // namespace

Disk::Disk(base::FilePath path, bool writable)
    : path_(std::move(path)), writable_(writable) {}

Disk::Disk(base::FilePath path, const Disk::Config& config)
    : path_(std::move(path)),
      writable_(config.writable),
      sparse_(config.sparse),
      o_direct_(config.o_direct),
      block_size_(config.block_size) {}

Disk::Disk(Disk&&) = default;

void Disk::EnableODirect(bool enable) {
  o_direct_ = enable;
}

void Disk::SetBlockSize(size_t block_size) {
  block_size_ = block_size;
}

base::StringPairs Disk::GetCrosvmArgs() const {
  std::string first;
  if (writable_)
    first = "--rwdisk";
  else
    first = "--disk";

  std::string sparse_arg{};
  if (sparse_) {
    sparse_arg = BooleanParameter(",sparse=", sparse_.value());
  }
  std::string o_direct_arg{};
  if (o_direct_) {
    o_direct_arg = BooleanParameter(",o_direct=", o_direct_.value());
  }
  std::string block_size_arg{};
  if (block_size_) {
    block_size_arg =
        base::StringPrintf(",block_size=%" PRIuS, block_size_.value());
  }

  std::string second =
      base::StrCat({path_.value(), sparse_arg, o_direct_arg, block_size_arg});
  base::StringPairs result = {{std::move(first), std::move(second)}};
  return result;
}

base::StringPairs Disk::GetVvuArgs() const {
  std::string first = "--file";
  std::string read_only_arg = "";
  if (!writable_)
    read_only_arg = ":read-only";

  std::string second = base::StrCat({path_.value(), read_only_arg});

  base::StringPairs result = {{std::move(first), std::move(second)}};
  return result;
}

Disk::~Disk() = default;

int64_t GetVmMemoryMiB() {
  int64_t sys_memory_mb = base::SysInfo::AmountOfPhysicalMemoryMB();
  int64_t vm_memory_mb;
  if (sys_memory_mb >= 4096) {
    // On devices with <=4GB RAM, reserve 1GB for other processes.
    vm_memory_mb = sys_memory_mb - 1024;
  } else {
    vm_memory_mb = sys_memory_mb / 4 * 3;
  }

  // Limit guest memory size to avoid running out of host process address space.
  //
  // A 32-bit process has 4GB address space, and some parts are not usable for
  // various reasons including address space layout randomization (ASLR).
  // In 32-bit crosvm address space, only ~3370MB is usable:
  // - 256MB is not usable because of executable load bias ASLR.
  // - 4MB is used for crosvm executable.
  // - 32MB it not usable because of heap ASLR.
  // - 16MB is used for mapped shared libraries.
  // - 256MB is not usable because of mmap base address ASLR.
  // - 132MB is used for gaps in the memory layout.
  // - 30MB is used for other allocations.
  //
  // 3328 is chosen because it's a rounded number (i.e. 3328 % 256 == 0).
  // TODO(hashimoto): Remove this once crosvm becomes 64-bit on ARM.
  constexpr int64_t k32bitVmMemoryMaxMb = 3328;
  if (sizeof(uintptr_t) == 4) {
    vm_memory_mb = std::min(vm_memory_mb, k32bitVmMemoryMaxMb);
  }

  return vm_memory_mb;
}

std::optional<int32_t> ReadFileToInt32(const base::FilePath& filename) {
  std::string str;
  int int_val;
  if (base::ReadFileToString(filename, &str) &&
      base::StringToInt(
          base::TrimWhitespaceASCII(str, base::TrimPositions::TRIM_TRAILING),
          &int_val)) {
    return std::optional<int32_t>(int_val);
  }

  return std::nullopt;
}

std::optional<int32_t> GetCpuPackageId(int32_t cpu) {
  base::FilePath topology_path(base::StringPrintf(
      "/sys/devices/system/cpu/cpu%d/topology/physical_package_id", cpu));
  return ReadFileToInt32(topology_path);
}

std::optional<int32_t> GetCpuCapacity(int32_t cpu) {
  base::FilePath cpu_capacity_path(
      base::StringPrintf("/sys/devices/system/cpu/cpu%d/cpu_capacity", cpu));
  return ReadFileToInt32(cpu_capacity_path);
}

std::optional<std::string> GetCpuAffinityFromClusters(
    const std::vector<std::vector<std::string>>& cpu_clusters,
    const std::map<int32_t, std::vector<std::string>>& cpu_capacity_groups) {
  if (cpu_clusters.size() > 1) {
    // If more than one CPU cluster exists, generate CPU affinity groups based
    // on clusters. Each CPU from a given cluster will be pinned to the full
    // set of cores of that cluster, allowing some scheduling flexibility
    // while still ensuring vCPUs can only run on physical cores from the same
    // package.
    std::vector<std::string> cpu_affinities;
    for (const auto& cluster : cpu_clusters) {
      auto cpu_list = base::JoinString(cluster, ",");
      for (const auto& cpu : cluster) {
        cpu_affinities.push_back(
            base::StringPrintf("%s=%s", cpu.c_str(), cpu_list.c_str()));
      }
    }
    return base::JoinString(cpu_affinities, ":");
  } else if (cpu_capacity_groups.size() > 1) {
    // If only one cluster exists, group CPUs by capacity if there are at least
    // two distinct CPU capacity groups.
    std::vector<std::string> cpu_affinities;
    for (const auto& group : cpu_capacity_groups) {
      auto cpu_list = base::JoinString(group.second, ",");
      for (const auto& cpu : group.second) {
        cpu_affinities.push_back(
            base::StringPrintf("%s=%s", cpu.c_str(), cpu_list.c_str()));
      }
    }
    return base::JoinString(cpu_affinities, ":");
  } else {
    return std::nullopt;
  }
}

bool SetUpCrosvmProcess(const base::FilePath& cpu_cgroup) {
  // Note: This function is meant to be called after forking a process for
  // crosvm but before execve(). Since Concierge is multi-threaded, this
  // function should not call any functions that are not async signal safe
  // (see man signal-safety). Especially, don't call malloc/new or any functions
  // or constructors that may allocate heap memory. Calling malloc/new may
  // result in a dead-lock trying to lock a mutex that has already been locked
  // by one of the parent's threads.

  // Set up CPU cgroup. Note that FilePath::value() returns a const reference
  // to std::string without allocating a new object. c_str() doesn't do any copy
  // as long as we use C++11 or later.
  const int fd =
      HANDLE_EINTR(open(cpu_cgroup.value().c_str(), O_WRONLY | O_CLOEXEC));
  if (fd < 0) {
    // TODO(yusukes): Do logging here in an async safe way.
    return false;
  }

  char pid_str[32];
  const size_t len = base::strings::SafeSPrintf(pid_str, "%d", getpid());
  const ssize_t written = HANDLE_EINTR(write(fd, pid_str, len));
  close(fd);
  if (written != len) {
    // TODO(yusukes): Do logging here in an async safe way.
    return false;
  }

  // Set up process group ID.
  return SetPgid();
}

bool SetPgid() {
  // Note: This should only call async-signal-safe functions. Don't call
  // malloc/new. See SetUpCrosvmProcess() for more details.

  if (setpgid(0, 0) != 0) {
    // TODO(yusukes): Do logging here in an async safe way.
    return false;
  }

  return true;
}

bool WaitForChild(pid_t child, base::TimeDelta timeout) {
  const base::Time deadline = base::Time::Now() + timeout;
  while (true) {
    pid_t ret = waitpid(child, nullptr, WNOHANG);
    if (ret == child || (ret < 0 && errno == ECHILD)) {
      // Either the child exited or it doesn't exist anymore.
      return true;
    }

    // ret == 0 means that the child is still alive
    if (ret < 0) {
      PLOG(ERROR) << "Failed to wait for child process";
      return false;
    }

    base::Time now = base::Time::Now();
    if (deadline <= now) {
      // Timed out.
      return false;
    }
    usleep(100);
  }
}

bool CheckProcessExists(pid_t pid) {
  if (pid == 0)
    return false;

  // Try to reap child process in case it just exited.
  waitpid(pid, NULL, WNOHANG);

  // kill() with a signal value of 0 is explicitly documented as a way to
  // check for the existence of a process.
  return kill(pid, 0) >= 0 || errno != ESRCH;
}

std::optional<BalloonStats> GetBalloonStats(std::string socket_path) {
  BalloonStats stats;
  if (!crosvm_client_balloon_stats(socket_path.c_str(), &stats.stats_ffi,
                                   &stats.balloon_actual)) {
    LOG(ERROR) << "Failed to retrieve balloon stats";
    return std::nullopt;
  }

  return stats;
}

std::optional<BalloonStats> ParseBalloonStats(
    const base::Value::Dict& balloon_stats) {
  auto additional_stats = balloon_stats.FindDict("stats");
  if (!additional_stats) {
    LOG(ERROR) << "stats dict not found";
    return std::nullopt;
  }

  BalloonStats stats;
  // Using FindDouble here since the value may exceeds 32bit integer range.
  // This is safe since double has 52bits of integer precision.
  stats.balloon_actual = static_cast<int64_t>(
      balloon_stats.FindDouble("balloon_actual").value_or(0));

  stats.stats_ffi.available_memory = static_cast<int64_t>(
      additional_stats->FindDouble("available_memory").value_or(0));
  stats.stats_ffi.disk_caches = static_cast<int64_t>(
      additional_stats->FindDouble("disk_caches").value_or(0));
  stats.stats_ffi.free_memory = static_cast<int64_t>(
      additional_stats->FindDouble("free_memory").value_or(0));
  stats.stats_ffi.major_faults = static_cast<int64_t>(
      additional_stats->FindDouble("major_faults").value_or(0));
  stats.stats_ffi.minor_faults = static_cast<int64_t>(
      additional_stats->FindDouble("minor_faults").value_or(0));
  stats.stats_ffi.swap_in =
      static_cast<int64_t>(additional_stats->FindDouble("swap_in").value_or(0));
  stats.stats_ffi.swap_out = static_cast<int64_t>(
      additional_stats->FindDouble("swap_out").value_or(0));
  stats.stats_ffi.total_memory = static_cast<int64_t>(
      additional_stats->FindDouble("total_memory").value_or(0));
  stats.stats_ffi.shared_memory = static_cast<int64_t>(
      additional_stats->FindDouble("shared_memory").value_or(0));
  stats.stats_ffi.unevictable_memory = static_cast<int64_t>(
      additional_stats->FindDouble("unevictable_memory").value_or(0));
  return stats;
}

bool AttachUsbDevice(std::string socket_path,
                     uint8_t bus,
                     uint8_t addr,
                     uint16_t vid,
                     uint16_t pid,
                     int fd,
                     uint8_t* out_port) {
  std::string device_path = "/proc/self/fd/" + std::to_string(fd);

  fcntl(fd, F_SETFD, 0);  // Remove the CLOEXEC

  return crosvm_client_usb_attach(socket_path.c_str(), bus, addr, vid, pid,
                                  device_path.c_str(), out_port);
}

bool DetachUsbDevice(std::string socket_path, uint8_t port) {
  return crosvm_client_usb_detach(socket_path.c_str(), port);
}

bool ListUsbDevice(std::string socket_path,
                   std::vector<UsbDeviceEntry>* device) {
  // Allocate enough slots for the max number of USB devices
  // This will never be more than 255
  const size_t max_usb_devices = crosvm_client_max_usb_devices();
  device->resize(max_usb_devices);

  ssize_t dev_count = crosvm_client_usb_list(socket_path.c_str(),
                                             device->data(), max_usb_devices);

  if (dev_count < 0) {
    return false;
  }

  device->resize(dev_count);

  return true;
}

bool CrosvmDiskResize(std::string socket_path,
                      int disk_index,
                      uint64_t new_size) {
  return crosvm_client_resize_disk(socket_path.c_str(), disk_index, new_size);
}

bool UpdateCpuShares(const base::FilePath& cpu_cgroup, int cpu_shares) {
  const std::string cpu_shares_str = std::to_string(cpu_shares);
  if (base::WriteFile(cpu_cgroup.Append("cpu.shares"), cpu_shares_str.c_str(),
                      cpu_shares_str.size()) != cpu_shares_str.size()) {
    PLOG(ERROR) << "Failed to update " << cpu_cgroup.value() << " to "
                << cpu_shares;
    return false;
  }
  return true;
}

// This will limit the tasks in the CGroup to P @percent of CPU.
// Although P can be > 100, its maximum value depends on the number of CPUs.
// For now, limit to a certain percent of 1 CPU. @percent=-1 disables quota.
bool UpdateCpuQuota(const base::FilePath& cpu_cgroup, int percent) {
  LOG_ASSERT(percent <= 100 && (percent >= 0 || percent == -1));

  // Set period to 100000us and quota to percent * 1000us.
  const std::string cpu_period_str = std::to_string(100000);
  const base::FilePath cfs_period_us = cpu_cgroup.Append("cpu.cfs_period_us");
  if (base::WriteFile(cfs_period_us, cpu_period_str.c_str(),
                      cpu_period_str.size()) != cpu_period_str.size()) {
    PLOG(ERROR) << "Failed to update " << cfs_period_us.value() << " to "
                << cpu_period_str;
    return false;
  }

  int quota_int;
  if (percent == -1)
    quota_int = -1;
  else
    quota_int = percent * 1000;

  const std::string cpu_quota_str = std::to_string(quota_int);
  const base::FilePath cfs_quota_us = cpu_cgroup.Append("cpu.cfs_quota_us");
  if (base::WriteFile(cfs_quota_us, cpu_quota_str.c_str(),
                      cpu_quota_str.size()) != cpu_quota_str.size()) {
    PLOG(ERROR) << "Failed to update " << cfs_quota_us.value() << " to "
                << cpu_quota_str;
    return false;
  }

  return true;
}

// Convert file path into fd path
// This will open the file and append SafeFD into provided container
std::string ConvertToFdBasedPath(brillo::SafeFD& parent_fd,
                                 base::FilePath* in_out_path,
                                 int flags,
                                 std::vector<brillo::SafeFD>& fd_storage) {
  static auto procSelfFd = base::FilePath("/proc/self/fd");
  if (procSelfFd.IsParent(*in_out_path)) {
    if (!base::PathExists(*in_out_path)) {
      return "Path does not exist";
    }
  } else {
    auto disk_fd = parent_fd.OpenExistingFile(*in_out_path, flags);
    if (brillo::SafeFD::IsError(disk_fd.second)) {
      LOG(ERROR) << "Could not open file: " << static_cast<int>(disk_fd.second);
      return "Could not open file";
    }
    *in_out_path = base::FilePath(kProcFileDescriptorsPath)
                       .Append(base::NumberToString(disk_fd.first.get()));
    fd_storage.push_back(std::move(disk_fd.first));
  }

  return "";
}

CustomParametersForDev::CustomParametersForDev(const std::string& data) {
  std::vector<base::StringPiece> lines = base::SplitStringPiece(
      data, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);

  for (auto& line : lines) {
    if (line.empty() || line[0] == '#')
      continue;

    // Line contains a prefix key. Remove all args with this prefix.
    if (line[0] == '!' && line.size() > 1) {
      const base::StringPiece prefix = line.substr(1, line.size() - 1);
      prefix_to_remove_.emplace_back(prefix);
      continue;
    }

    // Line contains a key only. Append the whole line.
    base::StringPairs pairs;
    if (!base::SplitStringIntoKeyValuePairs(line, '=', '\n', &pairs)) {
      params_to_add_.emplace_back(std::move(line), "");
      continue;
    }

    // Line contains a key-value pair.
    base::TrimWhitespaceASCII(pairs[0].first, base::TRIM_ALL, &pairs[0].first);
    base::TrimWhitespaceASCII(pairs[0].second, base::TRIM_ALL,
                              &pairs[0].second);
    if (pairs[0].first[0] == '-') {
      params_to_add_.emplace_back(std::move(pairs[0].first),
                                  std::move(pairs[0].second));
    } else {
      special_parameters_.emplace(std::move(pairs[0].first),
                                  std::move(pairs[0].second));
    }
  }
  initialized_ = true;
}

void CustomParametersForDev::Apply(base::StringPairs* args) {
  if (!initialized_)
    return;
  for (const auto& prefix : prefix_to_remove_) {
    base::EraseIf(*args, [&prefix](const auto& pair) {
      return base::StartsWith(pair.first, prefix);
    });
  }
  for (const auto& param : params_to_add_) {
    args->emplace_back(param.first, param.second);
  }
}

std::optional<const std::string> CustomParametersForDev::ObtainSpecialParameter(
    const std::string& key) {
  if (!initialized_)
    return std::nullopt;
  if (special_parameters_.find(key) != special_parameters_.end()) {
    return special_parameters_[key];
  } else {
    return std::nullopt;
  }
}

std::string CreateSharedDataParam(
    const base::FilePath& data_dir,
    const std::string& tag,
    bool enable_caches,
    bool ascii_casefold,
    bool posix_acl,
    const std::vector<uid_t>& privileged_quota_uids) {
  // TODO(b/169446394): Go back to using "never" when caching is disabled
  // once we can switch /data/media to use 9p.
  std::string result = base::StringPrintf(
      "%s:%s:type=fs:cache=%s:uidmap=%s:gidmap=%s:timeout=%d:rewrite-"
      "security-xattrs=true:ascii_casefold=%s:writeback=%s:posix_acl=%s",
      data_dir.value().c_str(), tag.c_str(), enable_caches ? "always" : "auto",
      kAndroidUidMap, kAndroidGidMap, enable_caches ? 3600 : 1,
      ascii_casefold ? "true" : "false", enable_caches ? "true" : "false",
      posix_acl ? "true" : "false");

  if (!privileged_quota_uids.empty()) {
    result += ":privileged_quota_uids=";
    for (size_t i = 0; i < privileged_quota_uids.size(); ++i) {
      if (i != 0)
        result += ' ';
      result += base::NumberToString(privileged_quota_uids[i]);
    }
  }
  return result;
}

std::string CreateFontsSharedDataParam() {
  return CreateSharedDataParam(base::FilePath(kFontsSharedDir),
                               kFontsSharedDirTag, true, false, true, {});
}

void ArcVmCPUTopology::CreateAffinity(void) {
  std::vector<std::string> cpu_list;
  std::vector<std::string> affinities;

  // Create capacity mask.
  int min_cap = -1;
  int last_non_rt_cpu = -1;
  for (const auto& cap : capacity_) {
    for (const auto cpu : cap.second) {
      if (cap.first)
        cpu_list.push_back(base::StringPrintf("%d=%d", cpu, cap.first));
      // last_non_rt_cpu should be the last cpu with a lowest capacity.
      if (min_cap == -1 || min_cap >= cap.first) {
        min_cap = cap.first;
        last_non_rt_cpu = cpu;
      }
    }
  }
  // Add RT VCPUs with a lowest capacity.
  if (min_cap) {
    for (int i = 0; i < num_rt_cpus_; i++) {
      cpu_list.push_back(base::StringPrintf("%d=%d", num_cpus_ + i, min_cap));
    }
    capacity_mask_ = base::JoinString(cpu_list, ",");
    cpu_list.clear();
  }

  for (const auto& pkg : package_) {
    bool is_rt_vcpu_package = false;
    for (auto cpu : pkg.second) {
      cpu_list.push_back(std::to_string(cpu));
      // Add RT VCPUs as a package with a lowest capacity.
      is_rt_vcpu_package = is_rt_vcpu_package || (cpu == last_non_rt_cpu);
    }
    if (is_rt_vcpu_package) {
      for (int i = 0; i < num_rt_cpus_; i++) {
        cpu_list.push_back(std::to_string(num_cpus_ + i));
      }
    }
    package_mask_.push_back(base::JoinString(cpu_list, ","));
    cpu_list.clear();
  }

  // Add RT VCPUs after non RT VCPUs.
  for (int i = 0; i < num_rt_cpus_; i++) {
    rt_cpus_.insert(num_cpus_ + i);
  }
  for (auto cpu : rt_cpus_) {
    cpu_list.push_back(std::to_string(cpu));
  }
  rt_cpu_mask_ = base::JoinString(cpu_list, ",");
  cpu_list.clear();

  for (int i = 0; i < num_cpus_ + num_rt_cpus_; i++) {
    if (rt_cpus_.find(i) == rt_cpus_.end()) {
      cpu_list.push_back(std::to_string(i));
    }
  }
  non_rt_cpu_mask_ = base::JoinString(cpu_list, ",");
  cpu_list.clear();

  // Try to group VCPUs based on physical CPUs topology.
  if (package_.size() > 1) {
    for (const auto& pkg : package_) {
      bool is_rt_vcpu_package = false;
      for (auto cpu : pkg.second) {
        cpu_list.push_back(std::to_string(cpu));
        // Add RT VCPUs as a package with a lowest capacity.
        is_rt_vcpu_package = is_rt_vcpu_package || (cpu == last_non_rt_cpu);
      }
      std::string cpu_mask = base::JoinString(cpu_list, ",");
      cpu_list.clear();
      for (auto cpu : pkg.second) {
        affinities.push_back(
            base::StringPrintf("%d=%s", cpu, cpu_mask.c_str()));
      }
      if (is_rt_vcpu_package) {
        for (int i = 0; i < num_rt_cpus_; i++) {
          affinities.push_back(
              base::StringPrintf("%d=%s", num_cpus_ + i, cpu_mask.c_str()));
        }
      }
    }
  } else {
    // Try to group VCPUs based on physical CPUs capacity values.
    for (const auto& cap : capacity_) {
      bool is_rt_vcpu_cap = false;
      for (auto cpu : cap.second) {
        cpu_list.push_back(std::to_string(cpu));
        is_rt_vcpu_cap = is_rt_vcpu_cap || (cpu == last_non_rt_cpu);
      }

      std::string cpu_mask = base::JoinString(cpu_list, ",");
      cpu_list.clear();
      for (auto cpu : cap.second) {
        affinities.push_back(
            base::StringPrintf("%d=%s", cpu, cpu_mask.c_str()));
      }
      if (is_rt_vcpu_cap) {
        for (int i = 0; i < num_rt_cpus_; i++) {
          affinities.push_back(
              base::StringPrintf("%d=%s", num_cpus_ + i, cpu_mask.c_str()));
        }
      }
    }
  }
  affinity_mask_ = base::JoinString(affinities, ":");

  num_cpus_ += num_rt_cpus_;
}

// Creates CPU grouping by cpu_capacity.
void ArcVmCPUTopology::CreateTopology(void) {
  for (uint32_t cpu = 0; cpu < num_cpus_; cpu++) {
    auto capacity = GetCpuCapacity(cpu);
    auto package = GetCpuPackageId(cpu);

    // Do not fail, carry on, but use an aritifical capacity group.
    if (!capacity)
      capacity_[0].push_back(cpu);
    else
      capacity_[*capacity].push_back(cpu);

    // Ditto.
    if (!package)
      package_[0].push_back(cpu);
    else
      package_[*package].push_back(cpu);
  }
}

// Check whether the host processor is symmetric.
// TODO(kansho): Support ADL. IsSymmetricCPU() would return true even though
//               it's heterogeneous.
bool ArcVmCPUTopology::IsSymmetricCPU() {
  return capacity_.size() == 1 && package_.size() == 1;
}

void ArcVmCPUTopology::CreateCPUAffinity() {
  CreateTopology();
  CreateAffinity();
}

void ArcVmCPUTopology::AddCpuToCapacityGroupForTesting(uint32_t cpu,
                                                       uint32_t capacity) {
  capacity_[capacity].push_back(cpu);
}

void ArcVmCPUTopology::AddCpuToPackageGroupForTesting(uint32_t cpu,
                                                      uint32_t package) {
  package_[package].push_back(cpu);
}

void ArcVmCPUTopology::CreateCPUAffinityForTesting() {
  CreateAffinity();
}

uint32_t ArcVmCPUTopology::NumCPUs() {
  return num_cpus_;
}

uint32_t ArcVmCPUTopology::NumRTCPUs() {
  return num_rt_cpus_;
}

void ArcVmCPUTopology::SetNumRTCPUs(uint32_t num_rt_cpus) {
  num_rt_cpus_ = num_rt_cpus;
}

const std::string& ArcVmCPUTopology::AffinityMask() {
  return affinity_mask_;
}

const std::string& ArcVmCPUTopology::RTCPUMask() {
  return rt_cpu_mask_;
}

const std::string& ArcVmCPUTopology::NonRTCPUMask() {
  return non_rt_cpu_mask_;
}

const std::string& ArcVmCPUTopology::CapacityMask() {
  return capacity_mask_;
}

const std::vector<std::string>& ArcVmCPUTopology::PackageMask() {
  return package_mask_;
}

ArcVmCPUTopology::ArcVmCPUTopology(uint32_t num_cpus, uint32_t num_rt_cpus) {
  num_cpus_ = num_cpus;
  num_rt_cpus_ = num_rt_cpus;
}

}  // namespace concierge
}  // namespace vm_tools
