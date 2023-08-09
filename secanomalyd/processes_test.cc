// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Unit tests for functionality in processes.h.

#include "secanomalyd/processes.h"

#include <map>
#include <optional>
#include <set>
#include <string>

#include <absl/strings/substitute.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>

#include <gtest/gtest.h>

#include <brillo/process/process_mock.h>
#include <sys/types.h>

namespace secanomalyd::testing {

namespace {

const void ExpectEqProcEntry(const ProcEntry& actual_pe,
                             const ProcEntry& expected_pe) {
  EXPECT_EQ(actual_pe.pid(), expected_pe.pid());
  EXPECT_EQ(actual_pe.ppid(), expected_pe.ppid());
  EXPECT_EQ(actual_pe.pidns(), expected_pe.pidns());
  EXPECT_EQ(actual_pe.mntns(), expected_pe.mntns());
  EXPECT_EQ(actual_pe.userns(), expected_pe.userns());
  EXPECT_EQ(actual_pe.comm(), expected_pe.comm());
  EXPECT_EQ(actual_pe.args(), expected_pe.args());
  EXPECT_EQ(actual_pe.sandbox_status(), expected_pe.sandbox_status());
}

const void ExpectProcEntryPids(const MaybeProcEntries& proc_entries,
                               std::set<pid_t> pids) {
  ASSERT_TRUE(proc_entries.has_value());
  ASSERT_EQ(proc_entries.value().size(), pids.size());
  for (auto pe : proc_entries.value()) {
    EXPECT_NE(pids.find(pe.pid()), pids.end());
  }
}

}  // namespace

class ProcessesTestFixture : public ::testing::Test {
  const std::string kStatusTemplate =
      "Name:	$0\n"
      "Umask:	0000\n"
      "State:	S (sleeping)\n"
      "Tgid:	1\n"
      "Ngid:	0\n"
      "Pid:	1\n"
      "PPid:	$1\n"
      "TracerPid:	0\n"
      "Uid:	$2	$2	$2	$2\n"
      "Gid:	0	0	0	0\n"
      "FDSize:	123\n"
      "Groups:  20162 20164 20166\n"
      "NStgid:	1\n"
      "NSpid:	1\n"
      "NSpgid:	1\n"
      "NSsid:	1\n"
      "VmPeak:	1024 kB\n"
      "VmSize:	1024 kB\n"
      "VmLck:	0 kB\n"
      "VmPin:	0 kB\n"
      "VmHWM:	1234 kB\n"
      "VmRSS:	1234 kB\n"
      "RssAnon:	1234 kB\n"
      "RssFile:	1234 kB\n"
      "RssShmem:	0 kB\n"
      "VmData:	1234 kB\n"
      "VmStk:	123 kB\n"
      "VmExe:	123 kB\n"
      "VmLib:	1234 kB\n"
      "VmPTE:	24 kB\n"
      "VmSwap:	0 kB\n"
      "CoreDumping:	0\n"
      "THP_enabled:	1\n"
      "Threads:	1\n"
      "SigQ:	1/12345\n"
      "SigPnd:	0000000000000000\n"
      "ShdPnd:	0000000000000000\n"
      "SigBlk:	0000000000000000\n"
      "SigIgn:	0000000000001000\n"
      "SigCgt:	0000000012345678\n"
      "CapInh:	0000000000000000\n"
      "CapPrm:	000003ffffffffff\n"
      "CapEff:	$3\n"
      "CapBnd:	000003ffffffffff\n"
      "CapAmb:	0000000000000000\n"
      "NoNewPrivs:	$4\n"
      "Seccomp:	$5\n"
      "Seccomp_filters:	0\n"
      "Speculation_Store_Bypass:	vulnerable\n"
      "SpeculationIndirectBranch:	always enabled\n"
      "Cpus_allowed:	ff\n"
      "Cpus_allowed_list:	0-7\n"
      "Mems_allowed:	1\n"
      "Mems_allowed_list:	0\n"
      "voluntary_ctxt_switches:	1234\n"
      "nonvoluntary_ctxt_switches:	4321";

 public:
  MaybeProcEntries ReadMockProcesses() { return std::nullopt; }
  ProcEntry CreateMockProcEntry(pid_t pid,
                                pid_t ppid,
                                ino_t pidns,
                                ino_t mntns,
                                ino_t userns,
                                std::string comm,
                                std::string args,
                                ProcEntry::SandboxStatus sandbox_status) {
    return ProcEntry(pid, ppid, pidns, mntns, userns, comm, args,
                     sandbox_status);
  }

 protected:
  struct MockProccess {
    std::string pid;
    std::string uid;
    std::string ppid;
    std::string name;
    std::string cap_eff;
    std::string no_new_privs;
    std::string seccomp;
    std::string cmdline;
    base::FilePath pid_ns_symlink;
    base::FilePath mnt_ns_symlink;
    base::FilePath user_ns_symlink;
  };

  // Creates a pristine procfs with a single mock process.
  void CreateFakeProcfs(MockProccess& proc, base::FilePath& pid_dir) {
    ASSERT_TRUE(fake_root_.CreateUniqueTempDir());
    base::FilePath proc_dir = fake_root_.GetPath().Append("proc");
    ASSERT_TRUE(base::CreateDirectory(proc_dir));
    pid_dir = proc_dir.Append(proc.pid);
    ASSERT_TRUE(base::CreateDirectory(pid_dir));
    CreateFakeProcDir(proc, pid_dir);
  }

  // Creates a pristine procfs with all processes listed in |mock_processes_|.
  void CreateFakeProcfs(base::FilePath& proc_dir) {
    ASSERT_TRUE(fake_root_.CreateUniqueTempDir());
    proc_dir = fake_root_.GetPath().Append("proc");
    ASSERT_TRUE(base::CreateDirectory(proc_dir));
    for (auto proc : mock_processes_) {
      base::FilePath pid_dir = proc_dir.Append(proc.second.pid);
      ASSERT_TRUE(base::CreateDirectory(pid_dir));
      CreateFakeProcDir(proc.second, pid_dir);
    }
  }

  void DestroyFakeProcfs() { ASSERT_TRUE(fake_root_.Delete()); }

  base::ScopedTempDir fake_root_;
  const std::set<pid_t> kAllProcs = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
  const std::set<pid_t> kInitPidNamespaceOnlyProcs = {1, 2, 3,  4, 5,
                                                      7, 8, 10, 11};
  const std::set<pid_t> kNoKernelTasksProcs = {1, 3, 4, 5, 6, 7, 8, 9, 11};
  const std::set<pid_t> kForbiddenIntersectionProcs = {1, 3, 6, 7, 8};
  // Each key corresponds to the name of the test.
  std::map<std::string, MockProccess> mock_processes_ = {
      {"InitProcess",
       {
           .pid = "1",
           .uid = "0",
           .ppid = "0",
           .name = "init",
           .cap_eff = "000001ffffffffff",
           .no_new_privs = "0",
           .seccomp = "0",
           .cmdline = "/sbin/init",
           .pid_ns_symlink = base::FilePath("pid:[4026531841]"),
           .mnt_ns_symlink = base::FilePath("mnt:[4026531836]"),
           .user_ns_symlink = base::FilePath("user:[4026531837]"),
       }},
      {"KernelThread",
       {
           .pid = "2",
           .uid = "0",
           .ppid = "0",
           .name = "kthreadd",
           .cap_eff = "000001ffffffffff",
           .no_new_privs = "0",
           .seccomp = "0",
           .cmdline = "",
           .pid_ns_symlink = base::FilePath("pid:[4026531841]"),
           .mnt_ns_symlink = base::FilePath("mnt:[4026531836]"),
           .user_ns_symlink = base::FilePath("user:[4026531837]"),
       }},
      {"NormalProcess",
       {
           .pid = "3",
           .uid = "0",
           .ppid = "1",
           .name = "normal_process",
           .cap_eff = "ffffffffffffffff",  // All caps present
           .no_new_privs = "0",
           .seccomp = "0",
           .cmdline = std::string("normal_process\0--start", 22),
           .pid_ns_symlink = base::FilePath("pid:[4026531841]"),
           .mnt_ns_symlink = base::FilePath("mnt:[4026531836]"),
           .user_ns_symlink = base::FilePath("user:[4026531837]"),
       }},
      {"NormalProcessSecure",
       {
           .pid = "4",
           .uid = "4",
           .ppid = "5",
           .name = "normal_process_secure",
           .cap_eff = "0000000000000000",  // No caps present
           .no_new_privs = "1",
           .seccomp = "2",
           .cmdline = std::string("normal_process\0--start", 22),
           .pid_ns_symlink = base::FilePath("pid:[4026531841]"),
           .mnt_ns_symlink = base::FilePath("mnt:[4026531836]"),
           .user_ns_symlink = base::FilePath("user:[4026531837]"),
       }},
      {"EmptyCmdline",
       {
           .pid = "5",
           .uid = "5",
           .ppid = "1",
           .name = "no_cmdline",
           .cap_eff = "ffffffffffdfffff",  // Only missing CAP_SYS_ADMIN
           .no_new_privs = "0",
           .seccomp = "0",
           .cmdline = "",
           .pid_ns_symlink = base::FilePath("pid:[4026531841]"),
           .mnt_ns_symlink = base::FilePath("mnt:[4026531836]"),
           .user_ns_symlink = base::FilePath("user:[4026531837]"),
       }},
      {"InvalidPIDNS",
       {
           .pid = "6",
           .uid = "6",
           .ppid = "1",
           .name = "invalid_pidns",
           .cap_eff = "0000000000200000",  // Only CAP_SYS_ADMIN present
           .no_new_privs = "0",
           .seccomp = "0",
           .cmdline = std::string("invalid_pidns\0--start", 21),
           .pid_ns_symlink = base::FilePath("abc"),
           .mnt_ns_symlink = base::FilePath("mnt:[4026531836]"),
           .user_ns_symlink = base::FilePath("user:[4026531837]"),
       }},
      {"InvalidPPID",
       {
           .pid = "7",
           .uid = "7",
           .ppid = "abc",
           .name = "invalid_ppid",
           .cap_eff = "efg",  // Invalid hex
           .no_new_privs = "0",
           .seccomp = "0",
           .cmdline = std::string("invalid_ppid\0--start", 20),
           .pid_ns_symlink = base::FilePath("pid:[4026531841]"),
           .mnt_ns_symlink = base::FilePath("mnt:[4026531836]"),
           .user_ns_symlink = base::FilePath("user:[4026531837]"),
       }},
      {"StatusReadFailure",  // Valid unless procfs is destroyed.
       {
           .pid = "8",
           .uid = "8",
           .ppid = "1",
           .name = "status_read_failure",
           .cap_eff = "000003ffffffffff",
           .no_new_privs = "0",
           .seccomp = "0",
           .cmdline = "",
           .pid_ns_symlink = base::FilePath("pid:[4026531841]"),
           .mnt_ns_symlink = base::FilePath("mnt:[4026531836]"),
           .user_ns_symlink = base::FilePath("user:[4026531837]"),
       }},
      {"InvalidPID",
       {
           .pid = "abc",
           .uid = "0",
           .ppid = "1",
           .name = "invalid_pid",
           .cap_eff = "000003ffffffffff",
           .no_new_privs = "0",
           .seccomp = "0",
           .cmdline = std::string("invalid_pid\0--start", 19),
           .pid_ns_symlink = base::FilePath("pid:[4026531841]"),
           .mnt_ns_symlink = base::FilePath("mnt:[4026531836]"),
           .user_ns_symlink = base::FilePath("user:[4026531837]"),
       }},
      {"NotInInitPidNs",
       {
           .pid = "9",
           .uid = "9",
           .ppid = "8",
           .name = "not_in_init_pid_ns",
           .cap_eff = "000003ffffffffff",
           .no_new_privs = "1",
           .seccomp = "1",
           .cmdline = std::string("not_in_init_pid_ns\0--start", 26),
           .pid_ns_symlink = base::FilePath("pid:[987654321]"),
           .mnt_ns_symlink = base::FilePath("mnt:[4026531836]"),
           .user_ns_symlink = base::FilePath("user:[4026531837]"),
       }},
      {"KernelTask",
       {
           .pid = "10",
           .uid = "0",
           .ppid = "2",
           .name = "kernel_task",
           .cap_eff = "ffffffffffffffff",
           .no_new_privs = "0",
           .seccomp = "0",
           .cmdline = "",
           .pid_ns_symlink = base::FilePath("pid:[4026531841]"),
           .mnt_ns_symlink = base::FilePath("mnt:[4026531836]"),
           .user_ns_symlink = base::FilePath("user:[4026531837]"),
       }},
      {"MinijailProcess",
       {
           .pid = "11",
           .uid = "0",
           .ppid = "1",
           .name = "minijail0",
           .cap_eff = "ffffffffffffffff",
           .no_new_privs = "0",
           .seccomp = "0",
           .cmdline = std::string("minijail0\0--config\0/usr/share/minijail/"
                                  "secagentd.conf\0--\0/usr/sbin/secagentd",
                                  74),
           .pid_ns_symlink = base::FilePath("pid:[4026531841]"),
           .mnt_ns_symlink = base::FilePath("mnt:[4026531836]"),
           .user_ns_symlink = base::FilePath("user:[4026531837]"),
       }},
  };

 private:
  void CreateFakeProcDir(MockProccess& mp, base::FilePath proc_dir) {
    // Generates content for the process status file, based on template.
    std::string status =
        absl::Substitute(kStatusTemplate, mp.name, mp.ppid, mp.uid, mp.cap_eff,
                         mp.no_new_privs, mp.seccomp);

    ASSERT_TRUE(base::WriteFile(proc_dir.Append("status"), status));
    ASSERT_TRUE(base::WriteFile(proc_dir.Append("cmdline"), mp.cmdline));

    const base::FilePath ns_dir = proc_dir.Append("ns");
    ASSERT_TRUE(base::CreateDirectory(ns_dir));
    ASSERT_TRUE(
        base::CreateSymbolicLink(mp.pid_ns_symlink, ns_dir.Append("pid")));
    ASSERT_TRUE(
        base::CreateSymbolicLink(mp.mnt_ns_symlink, ns_dir.Append("mnt")));
    ASSERT_TRUE(
        base::CreateSymbolicLink(mp.user_ns_symlink, ns_dir.Append("user")));
  }
};

TEST_F(ProcessesTestFixture, InitProcess) {
  std::string key = "InitProcess";
  base::FilePath pid_dir;
  ASSERT_NO_FATAL_FAILURE(CreateFakeProcfs(mock_processes_[key], pid_dir));
  ProcEntry expected_pe = CreateMockProcEntry(
      1, 0, 4026531841, 4026531836, 4026531837, mock_processes_[key].name,
      mock_processes_[key].cmdline, 0b000000);
  MaybeProcEntry actual_pe_ptr = ProcEntry::CreateFromPath(pid_dir);
  ASSERT_TRUE(actual_pe_ptr.has_value());
  ExpectEqProcEntry(actual_pe_ptr.value(), expected_pe);
}

TEST_F(ProcessesTestFixture, NormalProcess) {
  std::string key = "NormalProcess";
  base::FilePath pid_dir;
  ASSERT_NO_FATAL_FAILURE(CreateFakeProcfs(mock_processes_[key], pid_dir));
  ProcEntry expected_pe = CreateMockProcEntry(
      3, 1, 4026531841, 4026531836, 4026531837, mock_processes_[key].name,
      "normal_process --start", 0b000000);
  MaybeProcEntry actual_pe_ptr = ProcEntry::CreateFromPath(pid_dir);
  ASSERT_TRUE(actual_pe_ptr.has_value());
  ExpectEqProcEntry(actual_pe_ptr.value(), expected_pe);
}

TEST_F(ProcessesTestFixture, NormalProcessSecure) {
  std::string key = "NormalProcessSecure";
  base::FilePath pid_dir;
  ASSERT_NO_FATAL_FAILURE(CreateFakeProcfs(mock_processes_[key], pid_dir));
  ProcEntry expected_pe = CreateMockProcEntry(
      4, 5, 4026531841, 4026531836, 4026531837, mock_processes_[key].name,
      "normal_process --start", 0b111010);
  MaybeProcEntry actual_pe_ptr = ProcEntry::CreateFromPath(pid_dir);
  ASSERT_TRUE(actual_pe_ptr.has_value());
  ExpectEqProcEntry(actual_pe_ptr.value(), expected_pe);
}

TEST_F(ProcessesTestFixture, EmptyCmdline) {
  std::string key = "EmptyCmdline";
  base::FilePath pid_dir;
  ASSERT_NO_FATAL_FAILURE(CreateFakeProcfs(mock_processes_[key], pid_dir));
  ProcEntry expected_pe = CreateMockProcEntry(
      5, 1, 4026531841, 4026531836, 4026531837, mock_processes_[key].name,
      "[" + mock_processes_[key].name + "]", 0b110000);
  MaybeProcEntry actual_pe_ptr = ProcEntry::CreateFromPath(pid_dir);
  ASSERT_TRUE(actual_pe_ptr.has_value());
  ExpectEqProcEntry(actual_pe_ptr.value(), expected_pe);
}

TEST_F(ProcessesTestFixture, InvalidPIDNS) {
  std::string key = "InvalidPIDNS";
  base::FilePath pid_dir;
  ASSERT_NO_FATAL_FAILURE(CreateFakeProcfs(mock_processes_[key], pid_dir));
  ProcEntry expected_pe = CreateMockProcEntry(
      6, 1, 0, 4026531836, 4026531837, mock_processes_[key].name,
      "invalid_pidns --start", 0b010000);
  MaybeProcEntry actual_pe_ptr = ProcEntry::CreateFromPath(pid_dir);
  ASSERT_TRUE(actual_pe_ptr.has_value());
  ExpectEqProcEntry(actual_pe_ptr.value(), expected_pe);
}

TEST_F(ProcessesTestFixture, InvalidPPID) {
  std::string key = "InvalidPPID";
  base::FilePath pid_dir;
  ASSERT_NO_FATAL_FAILURE(CreateFakeProcfs(mock_processes_[key], pid_dir));
  ProcEntry expected_pe = CreateMockProcEntry(
      7, 0, 4026531841, 4026531836, 4026531837, mock_processes_[key].name,
      "invalid_ppid --start", 0b010000);
  MaybeProcEntry actual_pe_ptr = ProcEntry::CreateFromPath(pid_dir);
  ASSERT_TRUE(actual_pe_ptr.has_value());
  ExpectEqProcEntry(actual_pe_ptr.value(), expected_pe);
}

TEST_F(ProcessesTestFixture, StatusReadFailure) {
  std::string key = "StatusReadFailure";
  base::FilePath pid_dir;
  ASSERT_NO_FATAL_FAILURE(CreateFakeProcfs(mock_processes_[key], pid_dir));
  ASSERT_NO_FATAL_FAILURE(DestroyFakeProcfs());
  MaybeProcEntry actual_pe_ptr = ProcEntry::CreateFromPath(pid_dir);
  EXPECT_EQ(actual_pe_ptr, std::nullopt);
}

TEST_F(ProcessesTestFixture, InvalidPID) {
  std::string key = "InvalidPID";
  base::FilePath pid_dir;
  ASSERT_NO_FATAL_FAILURE(CreateFakeProcfs(mock_processes_[key], pid_dir));
  MaybeProcEntry actual_pe_ptr = ProcEntry::CreateFromPath(pid_dir);
  EXPECT_EQ(actual_pe_ptr, std::nullopt);
}

TEST_F(ProcessesTestFixture, ReadProcessesAll) {
  base::FilePath proc_dir;
  ASSERT_NO_FATAL_FAILURE(CreateFakeProcfs(proc_dir));
  MaybeProcEntries actual_proc_entries =
      ReadProcesses(ProcessFilter::kAll, proc_dir);
  ASSERT_TRUE(actual_proc_entries.has_value());
  EXPECT_EQ(actual_proc_entries.value().size(), kAllProcs.size());
  ExpectProcEntryPids(actual_proc_entries, kAllProcs);
}

TEST_F(ProcessesTestFixture, ReadProcessesInitNamespaceOnly) {
  base::FilePath proc_dir;
  ASSERT_NO_FATAL_FAILURE(CreateFakeProcfs(proc_dir));
  MaybeProcEntries actual_proc_entries =
      ReadProcesses(ProcessFilter::kInitPidNamespaceOnly, proc_dir);
  ASSERT_TRUE(actual_proc_entries.has_value());
  EXPECT_EQ(actual_proc_entries.value().size(),
            kInitPidNamespaceOnlyProcs.size());
  ExpectProcEntryPids(actual_proc_entries, kInitPidNamespaceOnlyProcs);
}

TEST_F(ProcessesTestFixture, ReadProcessesNoKernelTasks) {
  base::FilePath proc_dir;
  ASSERT_NO_FATAL_FAILURE(CreateFakeProcfs(proc_dir));
  MaybeProcEntries actual_proc_entries =
      ReadProcesses(ProcessFilter::kNoKernelTasks, proc_dir);
  ASSERT_TRUE(actual_proc_entries.has_value());
  EXPECT_EQ(actual_proc_entries.value().size(), kNoKernelTasksProcs.size());
  ExpectProcEntryPids(actual_proc_entries, kNoKernelTasksProcs);
}

TEST_F(ProcessesTestFixture, ForbiddenIntersectionProcs) {
  base::FilePath proc_dir;
  ASSERT_NO_FATAL_FAILURE(CreateFakeProcfs(proc_dir));
  MaybeProcEntries actual_proc_entries =
      ReadProcesses(ProcessFilter::kNoKernelTasks, proc_dir);
  ASSERT_TRUE(actual_proc_entries.has_value());

  MaybeProcEntry init_proc = GetInitProcEntry(actual_proc_entries.value());
  ASSERT_TRUE(init_proc.has_value());

  ProcEntries flagged_procs;
  std::copy_if(actual_proc_entries->begin(), actual_proc_entries->end(),
               std::back_inserter(flagged_procs), [&](const ProcEntry& e) {
                 return IsProcInForbiddenIntersection(e, init_proc.value());
               });
  MaybeProcEntries actual_forbidden_intersection_procs =
      MaybeProcEntries(flagged_procs);
  ASSERT_TRUE(actual_forbidden_intersection_procs.has_value());

  EXPECT_EQ(actual_forbidden_intersection_procs.value().size(),
            kForbiddenIntersectionProcs.size());

  ExpectProcEntryPids(actual_forbidden_intersection_procs,
                      kForbiddenIntersectionProcs);
}

}  // namespace secanomalyd::testing
