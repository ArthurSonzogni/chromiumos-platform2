// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SECANOMALYD_PROCESSES_H_
#define SECANOMALYD_PROCESSES_H_

#include <sys/types.h>

#include <bitset>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <base/files/file_path.h>

#include <brillo/process/process.h>

namespace secanomalyd {
using FilePaths = std::set<base::FilePath>;
const base::FilePath kProcPathBase("/proc");

namespace testing {
class ProcessesTestFixture;
}

class ProcEntry {
 public:
  // A given process can be sandboxed using zero or more mechanisms.
  using SandboxStatus = std::bitset<6>;
  static constexpr size_t kLandlockBit = 0;  // Least Significant Bit
  static constexpr size_t kSecCompBit = 1;
  static constexpr size_t kSELinuxBit = 2;
  static constexpr size_t kNoNewPrivsBit = 3;
  static constexpr size_t kNonRootBit = 4;
  static constexpr size_t kNoCapSysAdminBit = 5;

  static std::optional<ProcEntry> CreateFromPath(
      const base::FilePath& pid_path);

  // Copying the private fields is fine.
  ProcEntry(const ProcEntry& other) = default;
  ProcEntry& operator=(const ProcEntry& other) = default;

  pid_t pid() const { return pid_; }
  pid_t ppid() const { return ppid_; }
  ino_t pidns() const { return pidns_; }
  ino_t mntns() const { return mntns_; }
  ino_t userns() const { return userns_; }
  std::string comm() const { return comm_; }
  std::string args() const { return args_; }
  SandboxStatus sandbox_status() const { return sandbox_status_; }

  std::string FullDescription() const;

 private:
  friend class testing::ProcessesTestFixture;
  FRIEND_TEST(SignatureTest, SignatureForOneProc);
  FRIEND_TEST(SignatureTest, SignatureForMultipleProcs);
  FRIEND_TEST(ReporterTest, SimpleForbiddenIntersectionReport);
  FRIEND_TEST(ReporterTest, MountAndProcessAnomalyReport);
  FRIEND_TEST(ReporterTest, FullReport);

  ProcEntry(pid_t pid,
            pid_t ppid,
            ino_t pidns,
            ino_t mntns,
            ino_t userns,
            std::string comm,
            std::string args,
            SandboxStatus sandbox_status)
      : pid_(pid),
        ppid_(ppid),
        pidns_(pidns),
        mntns_(mntns),
        userns_(userns),
        comm_(comm),
        args_(args),
        sandbox_status_(sandbox_status) {}

  pid_t pid_;
  pid_t ppid_;
  ino_t pidns_;
  ino_t mntns_;
  ino_t userns_;
  std::string comm_;
  std::string args_;
  SandboxStatus sandbox_status_;
};

using MaybeProcEntry = std::optional<ProcEntry>;
using ProcEntries = std::vector<ProcEntry>;
using MaybeProcEntries = std::optional<ProcEntries>;

enum class ProcessFilter { kAll = 0, kInitPidNamespaceOnly, kNoKernelTasks };

MaybeProcEntries ReadProcesses(ProcessFilter filter,
                               const base::FilePath& proc = kProcPathBase);

// These functions filter processes by copying the appropriate entries from
// |all_procs| into |filtered_procs|. |FilterNinInitPidNsProcesses()| returns
// false if the init process is not found.
void FilterKernelProcesses(const ProcEntries& all_procs,
                           ProcEntries& filtered_procs);
bool FilterNonInitPidNsProcesses(const ProcEntries& all_procs,
                                 ProcEntries& filtered_procs);

bool IsProcInForbiddenIntersection(const ProcEntry& process,
                                   const ProcEntry& init_proc);
MaybeProcEntry GetInitProcEntry(const ProcEntries& proc_entries);

}  // namespace secanomalyd

#endif  // SECANOMALYD_PROCESSES_H_
