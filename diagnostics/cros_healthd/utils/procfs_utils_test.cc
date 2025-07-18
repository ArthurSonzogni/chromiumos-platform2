// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/utils/procfs_utils.h"

#include <sys/types.h>

#include <optional>

#include <base/files/file_path.h>
#include <base/logging.h>
#include <gtest/gtest.h>

#include "diagnostics/base/file_test_utils.h"
#include "diagnostics/base/file_utils.h"

namespace diagnostics {
namespace {

// Production instances will use a root directory of "/".
constexpr char kProductionRootDir[] = "/";

// Process ID to test with.
constexpr pid_t kProcessId = 42;

TEST(ProcfsUtilsTest, GetProcProcessDirectoryPath) {
  const auto process_dir = GetProcProcessDirectoryPath(
      base::FilePath(kProductionRootDir), kProcessId);
  EXPECT_EQ(process_dir.value(), "/proc/42");
}

TEST(ProcfsUtilsTest, GetProcCpuInfoPath) {
  const auto cpuinfo_path =
      GetProcCpuInfoPath(base::FilePath(kProductionRootDir));
  EXPECT_EQ(cpuinfo_path.value(), "/proc/cpuinfo");
}

TEST(ProcfsUtilsTest, GetProcStatPath) {
  const auto stat_path = GetProcStatPath(base::FilePath(kProductionRootDir));
  EXPECT_EQ(stat_path.value(), "/proc/stat");
}

TEST(ProcfsUtilsTest, GetProcUptimePath) {
  const auto uptime_path =
      GetProcUptimePath(base::FilePath(kProductionRootDir));
  EXPECT_EQ(uptime_path.value(), "/proc/uptime");
}

TEST(ProcfsUtilsTest, GetArcVmPidEmpty) {
  ScopedRootDirOverrides overrides;
  EXPECT_EQ(GetArcVmPid(GetRootDir()), std::nullopt);
}

TEST(ProcfsUtilsTest, GetArcVmPidNoCrosvm) {
  ScopedRootDirOverrides overrides;
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      GetRootDir().Append("proc/1/cmdline"), "/sbin/init"));
  EXPECT_EQ(GetArcVmPid(GetRootDir()), std::nullopt);
}

TEST(ProcfsUtilsTest, GetArcVmPidFound) {
  ScopedRootDirOverrides overrides;
  const char cmdline[] = "/usr/bin/crosvm\0--syslog-tag\0ARCVM(32)";
  ASSERT_TRUE(
      WriteFileAndCreateParentDirs(GetRootDir().Append("proc/2/cmdline"),
                                   std::string(cmdline, sizeof(cmdline) - 1)));
  auto pid = GetArcVmPid(GetRootDir());
  ASSERT_TRUE(pid.has_value());
  EXPECT_EQ(*pid, 2);
}

TEST(ProcfsUtilsTest, GetArcVmPidAnotherVM) {
  ScopedRootDirOverrides overrides;
  const char cmdline[] = "/usr/bin/crosvm\0--syslog-tag\0VM(32)";
  ASSERT_TRUE(
      WriteFileAndCreateParentDirs(GetRootDir().Append("proc/2/cmdline"),
                                   std::string(cmdline, sizeof(cmdline) - 1)));
  EXPECT_EQ(GetArcVmPid(GetRootDir()), std::nullopt);
}

TEST(ProcfsUtilsTest, IomemSuccess_Intel) {
  const std::string kContent = R"(
00000000-00000fff : Unknown E820 type
00001000-0009ffff : System RAM
000a0000-000fffff : Reserved
  000a0000-000bffff : PCI Bus 0000:00
00100000-99a29fff : System RAM
ff000000-ffffffff : INT0800:00
100000000-25e7fffff : System RAM
)";

  auto memory_info = ParseIomemContent(kContent);
  ASSERT_TRUE(memory_info.has_value());
  // Sum of "System RAM" ranges:
  // 0x9ffff-0x1000+1 + 0x99a29fff-0x100000+1 + 0x25e7fffff-0x100000000+1
  EXPECT_EQ(*memory_info, 8457588736);
}

TEST(ProcfsUtilsTest, IomemSuccess_ARM) {
  const std::string kContent = R"(
80000000-807fffff : System RAM
80c00000-85ffffff : System RAM
  80c10000-8245ffff : Kernel code
  82460000-825effff : reserved
  825f0000-828dffff : Kernel data
8ec00000-8f5fffff : System RAM
8fb00000-940fffff : System RAM
94300000-943fffff : System RAM
94e00000-bfffbfff : System RAM
c0000000-ffdfffff : System RAM
100000000-27fffffff : System RAM
)";

  auto memory_info = ParseIomemContent(kContent);
  ASSERT_TRUE(memory_info.has_value());
  // Sum of "System RAM" ranges:
  EXPECT_EQ(*memory_info, 8419000320);
}

TEST(ProcfsUtilsTest, IomemEmpty) {
  auto memory_info = ParseIomemContent("");
  EXPECT_FALSE(memory_info.has_value());
}

TEST(ProcfsUtilsTest, IomemFormattedIncorrectly) {
  const std::string kContent = "Incorrectly formatted meminfo contents.\n";

  auto memory_info = ParseIomemContent(kContent);
  EXPECT_FALSE(memory_info.has_value());
}

TEST(ProcfsUtilsTest, IomemNoSystemRAM) {
  const std::string kContent = R"(
00000000-00000fff : Unknown E820 type
000a0000-000fffff : Reserved
ff000000-ffffffff : INT0800:00
)";

  auto memory_info = ParseIomemContent(kContent);
  EXPECT_FALSE(memory_info.has_value());
}

TEST(ProcfsUtilsTest, IomemIncorrectlyFormattedRanges) {
  // ` ` instead of `-`.
  const std::string kContent = R"(
00001000 0009ffff : System RAM
00100000 99a29fff : System RAM
100000000 25e7fffff : System RAM
)";

  auto memory_info = ParseIomemContent(kContent);
  EXPECT_FALSE(memory_info.has_value());
}

TEST(ProcfsUtilsTest, SmapsEmpty) {
  const std::string kContent = "";

  auto smaps = ParseProcSmaps(kContent);
  EXPECT_FALSE(smaps.has_value());
}

TEST(ProcfsUtilsTest, SmapsNoArcVmGuest) {
  const std::string kContent = R"(
56ad6eb41000-56ad6f91e000 r-xp 00000000 b3:03 21058    /usr/bin/crosvm
Size:              14196 kB
Rss:                2940 kB
Swap:                  0 kB
56ad6f91e000-56ad6f9b5000 r--p 00ddc000 b3:03 21058    /usr/bin/crosvm
Size:                604 kB
Rss:                 120 kB
Swap:                484 kB
)";

  auto smaps = ParseProcSmaps(kContent);
  EXPECT_FALSE(smaps.has_value());
}

TEST(ProcfsUtilsTest, SmapsSuccess) {
  const std::string kContent = R"(
7980712e6000-79813e7e6000 rw-s 00100000 00:01 164    /memfd:crosvm_guest
Size:            3363840 kB
Rss:             2243936 kB
Swap:             490460 kB
79813e846000-79813e8e6000 rw-s 00000000 00:01 164    /memfd:crosvm_guest
Size:                640 kB
Rss:                 228 kB
Swap:                408 kB
)";

  auto smaps = ParseProcSmaps(kContent);
  ASSERT_TRUE(smaps.has_value());
  // Sum of "Rss" sizes:
  // (2243936 + 228) * 1024
  EXPECT_EQ(smaps->crosvm_guest_rss, 2298023936);
  // Sum of "Swap" sizes:
  // (490460 + 408) * 1024
  EXPECT_EQ(smaps->crosvm_guest_swap, 502648832);
}

TEST(ProcfsUtilsTest, SmapsSuccessMixedContent) {
  const std::string kContent = R"(
56ad6eb41000-56ad6f91e000 r-xp 00000000 b3:03 21058 /usr/bin/crosvm
Size:              14196 kB
Rss:                2940 kB
Swap:                  0 kB
7980712e6000-79813e7e6000 rw-s 00100000 00:01 164 /memfd:crosvm_guest (deleted)
Size:            3363840 kB
Rss:             2243936 kB
Swap:             490460 kB
56ad6f91e000-56ad6f9b5000 r--p 00ddc000 b3:03 21058 /usr/bin/crosvm
Size:                604 kB
Rss:                 120 kB
Swap:                484 kB
79813e846000-79813e8e6000 rw-s 00000000 00:01 164 /memfd:crosvm_guest (deleted)
Size:                640 kB
Rss:                 228 kB
Swap:                408 kB
)";

  auto smaps = ParseProcSmaps(kContent);
  ASSERT_TRUE(smaps.has_value());
  // Sum of "Rss" sizes: (LL for avoiding integer overflow)
  EXPECT_EQ(smaps->crosvm_guest_rss, (2243936LL + 228) * 1024);
  // Sum of "Swap" sizes:
  EXPECT_EQ(smaps->crosvm_guest_swap, (490460 + 408) * 1024);
}

}  // namespace
}  // namespace diagnostics
