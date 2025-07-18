// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_UTILS_PROCFS_UTILS_H_
#define DIAGNOSTICS_CROS_HEALTHD_UTILS_PROCFS_UTILS_H_

#include <sys/types.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace base {
class FilePath;
}  // namespace base

namespace diagnostics {

// Indices of fields of interest in /proc/[pid]/stat. These should be kept in
// numerical order. Note that this is not an enum class so that it can be
// implicitly converted to ints when used as an index into an array or vector.
enum ProcPidStatIndices {
  kProcessID = 0,
  kName = 1,
  kState = 2,
  kParentProcessID = 3,
  kProcessGroupID = 4,
  kPriority = 17,
  kNice = 18,
  kThreads = 19,
  kStartTime = 21,
  kMaxValue = kStartTime,  // Must be updated whenever a larger index is added.
};

// Files read from a process subdirectory of procfs.
extern const char kProcessCmdlineFile[];
extern const char kProcessStatFile[];
extern const char kProcessStatmFile[];
extern const char kProcessStatusFile[];
extern const char kProcessIOFile[];

// Information collected from /proc/PID/smaps.
// The crosvm guest information is computed by looking at memory regions
// marked as "*/memfd:crosvm_guest*".
struct ProcSmaps {
  // Total RSS size of the crosvm guest in bytes.
  int64_t crosvm_guest_rss = 0;
  // Total swap size of the crosvm guest in byptes.
  int64_t crosvm_guest_swap = 0;

  // Used to check if any bits of information is collected.
  bool operator==(const ProcSmaps& other) const = default;
};

// Returns an absolute path to the procfs subdirectory containing files related
// to the process with ID |pid|. On a real device, this will be /proc/|pid|.
base::FilePath GetProcProcessDirectoryPath(const base::FilePath& root_dir,
                                           pid_t pid);

// Returns an absolute path to the cpuinfo file in procfs. On a real device,
// this will be /proc/cpuinfo.
base::FilePath GetProcCpuInfoPath(const base::FilePath& root_dir);

// Returns an absolute path to the stat file in procfs. On a real device, this
// will be /proc/stat.
base::FilePath GetProcStatPath(const base::FilePath& root_dir);

// Returns an absolute path to the uptime file in procfs. On a real device, this
// will be /proc/uptime.
base::FilePath GetProcUptimePath(const base::FilePath& root_dir);

// Returns an absolute path to the crypto file in procfs. On a real device,
// this will be /proc/crypto.
base::FilePath GetProcCryptoPath(const base::FilePath& root_dir);

// Gets the PID of ARCVM by traversing /proc/*/cmdline.
// Returns nullopt on error.
//
// Other approaches were considered but those didn't work:
// 1) Ask concierge to return the PID of crosvm
//    Didn't work because concierge is running in a PID namespace.
// 2) Ask concierge to read crosvm's smaps file
//    Didn't work because concierge does not have CAP_SYS_PTRACE
//    which is required to read smaps files.
std::optional<int> GetArcVmPid(const base::FilePath& root_dir);

// Gets the total memory size as bytes from /proc/iomem content.
// Returns std::nullopt on error.
std::optional<uint64_t> ParseIomemContent(std::string_view content);

// Gets the memory information from /proc/PID/smaps content.
// Returns std::nullopt on error or no information is collected.
std::optional<ProcSmaps> ParseProcSmaps(std::string_view content);

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_UTILS_PROCFS_UTILS_H_
