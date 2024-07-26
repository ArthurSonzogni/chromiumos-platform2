// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/system/meminfo_reader.h"

#include <string>

#include <gtest/gtest.h>

#include "diagnostics/base/file_test_utils.h"

namespace diagnostics {
namespace {

class MeminfoReaderTest : public BaseFileTest {
 public:
  MeminfoReaderTest(const MeminfoReaderTest&) = delete;
  MeminfoReaderTest& operator=(const MeminfoReaderTest&) = delete;

 protected:
  MeminfoReaderTest() = default;

  void SetMockMemoryInfo(const std::string& info) {
    SetFile({"proc", "meminfo"}, info);
  }

  MeminfoReader meminfo_reader_;
};

TEST_F(MeminfoReaderTest, MeminfoSuccess) {
  SetMockMemoryInfo(
      "MemTotal:        3906320 kB\n"
      "MemFree:         873180 kB\n"
      "MemAvailable:    87980 kB\n");

  auto memory_info = meminfo_reader_.GetInfo();
  EXPECT_TRUE(memory_info.has_value());
  EXPECT_EQ(memory_info.value().total_memory_kib, 3906320);
  EXPECT_EQ(memory_info.value().free_memory_kib, 873180);
  EXPECT_EQ(memory_info.value().available_memory_kib, 87980);
}

TEST_F(MeminfoReaderTest, MeminfoNoFile) {
  auto memory_info = meminfo_reader_.GetInfo();
  EXPECT_FALSE(memory_info.has_value());
}

TEST_F(MeminfoReaderTest, MeminfoFormattedIncorrectly) {
  SetMockMemoryInfo("Incorrectly formatted meminfo contents.\n");

  auto memory_info = meminfo_reader_.GetInfo();
  EXPECT_FALSE(memory_info.has_value());
}

TEST_F(MeminfoReaderTest, MeminfoNoMemTotal) {
  SetMockMemoryInfo(
      "MemFree:         873180 kB\n"
      "MemAvailable:    87980 kB\n");

  auto memory_info = meminfo_reader_.GetInfo();
  EXPECT_FALSE(memory_info.has_value());
}

TEST_F(MeminfoReaderTest, MeminfoNoMemFree) {
  SetMockMemoryInfo(
      "MemTotal:        3906320 kB\n"
      "MemAvailable:    87980 kB\n");

  auto memory_info = meminfo_reader_.GetInfo();
  EXPECT_FALSE(memory_info.has_value());
}

TEST_F(MeminfoReaderTest, MeminfoNoMemAvailable) {
  SetMockMemoryInfo(
      "MemTotal:        3906320 kB\n"
      "MemFree:         873180 kB\n");

  auto memory_info = meminfo_reader_.GetInfo();
  EXPECT_FALSE(memory_info.has_value());
}

TEST_F(MeminfoReaderTest, MeminfoIncorrectlyFormattedMemTotal) {
  // No space between memory amount and unit.
  SetMockMemoryInfo(
      "MemTotal:        3906320kB\n"
      "MemFree:         873180 kB\n"
      "MemAvailable:    87980 kB\n");

  auto memory_info = meminfo_reader_.GetInfo();
  EXPECT_FALSE(memory_info.has_value());
}

TEST_F(MeminfoReaderTest, MeminfoIncorrectlyFormattedMemFree) {
  SetMockMemoryInfo(
      "MemTotal:        3906320 kB\n"
      "MemFree:         873180 WrongUnit\n"
      "MemAvailable:    87980 kB\n");

  auto memory_info = meminfo_reader_.GetInfo();
  EXPECT_FALSE(memory_info.has_value());
}

TEST_F(MeminfoReaderTest, MeminfoIncorrectlyFormattedMemAvailable) {
  SetMockMemoryInfo(
      "MemTotal:        3906320 kB\n"
      "MemFree:         873180 kB\n"
      "MemAvailable:    NotAnInteger kB\n");

  auto memory_info = meminfo_reader_.GetInfo();
  EXPECT_FALSE(memory_info.has_value());
}

}  // namespace
}  // namespace diagnostics
