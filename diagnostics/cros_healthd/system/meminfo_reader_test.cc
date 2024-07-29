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
