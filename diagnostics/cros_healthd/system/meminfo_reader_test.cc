// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/system/meminfo_reader.h"

#include <string>

#include <base/strings/strcat.h>
#include <gtest/gtest.h>

#include "diagnostics/base/file_test_utils.h"

namespace diagnostics {
namespace {

struct FakeMeminfoContent {
  std::string mem_total = "MemTotal:        0 kB\n";
  std::string mem_free = "MemFree:         0 kB\n";
  std::string mem_available = "MemAvailable:    0 kB\n";
  std::string buffers = "Buffers:         0 kB\n";
  std::string cached = "Cached:          0 kB\n";
  std::string shared_mem = "Shmem:           0 kB\n";
  std::string active = "Active:          0 kB\n";
  std::string inactive = "Inactive:        0 kB\n";
  std::string swap_total = "SwapTotal:       0 kB\n";
  std::string swap_free = "SwapFree:        0 kB\n";
  std::string swap_cached = "SwapCached:      0 kB\n";
  std::string slab = "Slab:            0 kB\n";
  std::string slab_reclaim = "SReclaimable:    0 kB\n";
  std::string slab_unreclaim = "SUnreclaim:      0 kB\n";
};

class MeminfoReaderTest : public BaseFileTest {
 public:
  MeminfoReaderTest(const MeminfoReaderTest&) = delete;
  MeminfoReaderTest& operator=(const MeminfoReaderTest&) = delete;

 protected:
  MeminfoReaderTest() = default;

  void SetFakeMemoryInfo(const FakeMeminfoContent& fake_meminfo) {
    SetFile({"proc", "meminfo"}, base::StrCat({
                                     fake_meminfo.mem_total,
                                     fake_meminfo.mem_free,
                                     fake_meminfo.mem_available,
                                     fake_meminfo.buffers,
                                     fake_meminfo.cached,
                                     fake_meminfo.shared_mem,
                                     fake_meminfo.active,
                                     fake_meminfo.inactive,
                                     fake_meminfo.swap_total,
                                     fake_meminfo.swap_free,
                                     fake_meminfo.swap_cached,
                                     fake_meminfo.slab,
                                     fake_meminfo.slab_reclaim,
                                     fake_meminfo.slab_unreclaim,
                                 }));
  }

  MeminfoReader meminfo_reader_;
};

TEST_F(MeminfoReaderTest, TotalMemory) {
  SetFakeMemoryInfo({.mem_total = "MemTotal:        3906320 kB\n"});
  auto memory_info = meminfo_reader_.GetInfo();
  ASSERT_TRUE(memory_info.has_value());
  EXPECT_EQ(memory_info.value().total_memory_kib, 3906320);
}

TEST_F(MeminfoReaderTest, NoTotalMemory) {
  SetFakeMemoryInfo({.mem_total = ""});
  EXPECT_FALSE(meminfo_reader_.GetInfo().has_value());
}

TEST_F(MeminfoReaderTest, FreeMemory) {
  SetFakeMemoryInfo({.mem_free = "MemFree:         873180 kB\n"});
  auto memory_info = meminfo_reader_.GetInfo();
  ASSERT_TRUE(memory_info.has_value());
  EXPECT_EQ(memory_info.value().free_memory_kib, 873180);
}

TEST_F(MeminfoReaderTest, NoFreeMemory) {
  SetFakeMemoryInfo({.mem_free = ""});
  EXPECT_FALSE(meminfo_reader_.GetInfo().has_value());
}

TEST_F(MeminfoReaderTest, AvailableMemory) {
  SetFakeMemoryInfo({.mem_available = "MemAvailable:    87980 kB\n"});
  auto memory_info = meminfo_reader_.GetInfo();
  ASSERT_TRUE(memory_info.has_value());
  EXPECT_EQ(memory_info.value().available_memory_kib, 87980);
}

TEST_F(MeminfoReaderTest, NoAvailableMemory) {
  SetFakeMemoryInfo({.mem_available = ""});
  EXPECT_FALSE(meminfo_reader_.GetInfo().has_value());
}

TEST_F(MeminfoReaderTest, Buffers) {
  SetFakeMemoryInfo({.buffers = "Buffers:         166684 kB\n"});
  auto memory_info = meminfo_reader_.GetInfo();
  ASSERT_TRUE(memory_info.has_value());
  EXPECT_EQ(memory_info.value().buffers_kib, 166684);
}

TEST_F(MeminfoReaderTest, NoBuffers) {
  SetFakeMemoryInfo({.buffers = ""});
  EXPECT_FALSE(meminfo_reader_.GetInfo().has_value());
}

TEST_F(MeminfoReaderTest, Cached) {
  SetFakeMemoryInfo({.cached = "Cached:          1455512 kB\n"});
  auto memory_info = meminfo_reader_.GetInfo();
  ASSERT_TRUE(memory_info.has_value());
  EXPECT_EQ(memory_info.value().page_cache_kib, 1455512);
}

TEST_F(MeminfoReaderTest, NoCached) {
  SetFakeMemoryInfo({.cached = ""});
  EXPECT_FALSE(meminfo_reader_.GetInfo().has_value());
}

TEST_F(MeminfoReaderTest, SharedMemory) {
  SetFakeMemoryInfo({.shared_mem = "Shmem:           283464 kB\n"});
  auto memory_info = meminfo_reader_.GetInfo();
  ASSERT_TRUE(memory_info.has_value());
  EXPECT_EQ(memory_info.value().shared_memory_kib, 283464);
}

TEST_F(MeminfoReaderTest, NoSharedMemory) {
  SetFakeMemoryInfo({.shared_mem = ""});
  EXPECT_FALSE(meminfo_reader_.GetInfo().has_value());
}

TEST_F(MeminfoReaderTest, ActiveMemory) {
  SetFakeMemoryInfo({.active = "Active:          1718544 kB\n"});
  auto memory_info = meminfo_reader_.GetInfo();
  ASSERT_TRUE(memory_info.has_value());
  EXPECT_EQ(memory_info.value().active_memory_kib, 1718544);
}

TEST_F(MeminfoReaderTest, NoActiveMemory) {
  SetFakeMemoryInfo({.active = ""});
  EXPECT_FALSE(meminfo_reader_.GetInfo().has_value());
}

TEST_F(MeminfoReaderTest, InactiveMemory) {
  SetFakeMemoryInfo({.inactive = "Inactive:        970260 kB\n"});
  auto memory_info = meminfo_reader_.GetInfo();
  ASSERT_TRUE(memory_info.has_value());
  EXPECT_EQ(memory_info.value().inactive_memory_kib, 970260);
}

TEST_F(MeminfoReaderTest, NoInactiveMemory) {
  SetFakeMemoryInfo({.inactive = ""});
  EXPECT_FALSE(meminfo_reader_.GetInfo().has_value());
}

TEST_F(MeminfoReaderTest, TotalSwapMemory) {
  SetFakeMemoryInfo({.swap_total = "SwapTotal:       16000844 kB\n"});
  auto memory_info = meminfo_reader_.GetInfo();
  ASSERT_TRUE(memory_info.has_value());
  EXPECT_EQ(memory_info.value().total_swap_memory_kib, 16000844);
}

TEST_F(MeminfoReaderTest, NoTotalSwapMemory) {
  SetFakeMemoryInfo({.swap_total = ""});
  EXPECT_FALSE(meminfo_reader_.GetInfo().has_value());
}

TEST_F(MeminfoReaderTest, FreeSwapMemory) {
  SetFakeMemoryInfo({.swap_free = "SwapFree:        16000422 kB\n"});
  auto memory_info = meminfo_reader_.GetInfo();
  ASSERT_TRUE(memory_info.has_value());
  EXPECT_EQ(memory_info.value().free_swap_memory_kib, 16000422);
}

TEST_F(MeminfoReaderTest, NoFreeSwapMemory) {
  SetFakeMemoryInfo({.swap_free = ""});
  EXPECT_FALSE(meminfo_reader_.GetInfo().has_value());
}

TEST_F(MeminfoReaderTest, CachedSwapMemory) {
  SetFakeMemoryInfo({.swap_cached = "SwapCached:      132 kB\n"});
  auto memory_info = meminfo_reader_.GetInfo();
  ASSERT_TRUE(memory_info.has_value());
  EXPECT_EQ(memory_info.value().cached_swap_memory_kib, 132);
}

TEST_F(MeminfoReaderTest, NoCachedSwapMemory) {
  SetFakeMemoryInfo({.swap_cached = ""});
  EXPECT_FALSE(meminfo_reader_.GetInfo().has_value());
}

TEST_F(MeminfoReaderTest, TotalSlabMemory) {
  SetFakeMemoryInfo({.slab = "Slab:            317140 kB\n"});
  auto memory_info = meminfo_reader_.GetInfo();
  ASSERT_TRUE(memory_info.has_value());
  EXPECT_EQ(memory_info.value().total_slab_memory_kib, 317140);
}

TEST_F(MeminfoReaderTest, NoTotalSlabMemory) {
  SetFakeMemoryInfo({.slab = ""});
  EXPECT_FALSE(meminfo_reader_.GetInfo().has_value());
}

TEST_F(MeminfoReaderTest, ReclaimableSlabMemory) {
  SetFakeMemoryInfo({.slab_reclaim = "SReclaimable:    194160 kB\n"});
  auto memory_info = meminfo_reader_.GetInfo();
  ASSERT_TRUE(memory_info.has_value());
  EXPECT_EQ(memory_info.value().reclaimable_slab_memory_kib, 194160);
}

TEST_F(MeminfoReaderTest, NoReclaimableSlabMemory) {
  SetFakeMemoryInfo({.slab_reclaim = ""});
  EXPECT_FALSE(meminfo_reader_.GetInfo().has_value());
}

TEST_F(MeminfoReaderTest, UnreclaimableSlabMemory) {
  SetFakeMemoryInfo({.slab_unreclaim = "SUnreclaim:      122980 kB\n"});
  auto memory_info = meminfo_reader_.GetInfo();
  ASSERT_TRUE(memory_info.has_value());
  EXPECT_EQ(memory_info.value().unreclaimable_slab_memory_kib, 122980);
}

TEST_F(MeminfoReaderTest, NoUnreclaimableSlabMemory) {
  SetFakeMemoryInfo({.slab_unreclaim = ""});
  EXPECT_FALSE(meminfo_reader_.GetInfo().has_value());
}

TEST_F(MeminfoReaderTest, MissingMeminfoFile) {
  UnsetPath({"proc", "meminfo"});
  EXPECT_FALSE(meminfo_reader_.GetInfo().has_value());
}

TEST_F(MeminfoReaderTest, MeminfoFormattedIncorrectly) {
  SetFile({"proc", "meminfo"}, "Incorrectly formatted meminfo contents.\n");
  EXPECT_FALSE(meminfo_reader_.GetInfo().has_value());
}

TEST_F(MeminfoReaderTest, IncorrectFormat) {
  // No space between memory amount and unit.
  SetFakeMemoryInfo({.mem_total = "MemTotal:        3906320kB\n"});
  EXPECT_FALSE(meminfo_reader_.GetInfo().has_value());
}

TEST_F(MeminfoReaderTest, WrongUnit) {
  SetFakeMemoryInfo({.mem_free = "MemFree:         873180 WrongUnit\n"});
  EXPECT_FALSE(meminfo_reader_.GetInfo().has_value());
}

TEST_F(MeminfoReaderTest, InvalidInteger) {
  SetFakeMemoryInfo({.mem_available = "MemAvailable:    NotAnInteger kB\n"});
  EXPECT_FALSE(meminfo_reader_.GetInfo().has_value());
}

}  // namespace
}  // namespace diagnostics
