// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/utils/procfs_utils.h"

#include <sys/types.h>

#include <base/files/file_path.h>
#include <gtest/gtest.h>

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
  EXPECT_EQ(8457588736, *memory_info);
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
  EXPECT_EQ(8419000320, *memory_info);
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

}  // namespace
}  // namespace diagnostics
