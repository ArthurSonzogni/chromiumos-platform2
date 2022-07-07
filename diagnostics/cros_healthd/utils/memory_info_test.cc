// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <stdint.h>

#include <optional>
#include <string>

#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_tokenizer.h>

#include "diagnostics/common/file_test_utils.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "diagnostics/cros_healthd/utils/memory_info.h"

namespace diagnostics {
namespace {

constexpr char kRelativeMeminfoPath[] = "proc/meminfo";
constexpr uint32_t kFakeMeminfoMemTotal = 3906320;
constexpr uint32_t kFakeMeminfonMemFree = 873180;
constexpr uint32_t kFakeMeminfonMemAvailable = 87980;
constexpr char kFakeMeminfoContents[] =
    "MemTotal:      3906320 kB\nMemFree:      873180 kB\nMemAvailable:      "
    "87980 kB\n";
constexpr char kFakeMeminfoContentsIncorrectlyFormattedFile[] =
    "Incorrectly formatted meminfo contents.\n";
constexpr char kFakeMeminfoContentsMissingMemtotal[] =
    "MemFree:      873180 kB\nMemAvailable:      87980 kB\n";
constexpr char kFakeMeminfoContentsMissingMemfree[] =
    "MemTotal:      3906320 kB\nMemAvailable:      87980 kB\n";
constexpr char kFakeMeminfoContentsMissingMemavailable[] =
    "MemTotal:      3906320 kB\nMemFree:      873180 kB\n";
constexpr char kFakeMeminfoContentsIncorrectlyFormattedMemtotal[] =
    "MemTotal:      3906320kB\nMemFree:      873180 kB\nMemAvailable:      "
    "87980 kB\n";
constexpr char kFakeMeminfoContentsIncorrectlyFormattedMemfree[] =
    "MemTotal:      3906320 kB\nMemFree:      873180 WrongUnits\nMemAvailable: "
    "     87980 kB\n";
constexpr char kFakeMeminfoContentsIncorrectlyFormattedMemavailable[] =
    "MemTotal:      3906320 kB\nMemFree:      873180 kB\nMemAvailable:      "
    "NotAnInteger kB\n";

class MemoryInfoTest : public ::testing::Test {
 protected:
  MemoryInfoTest() = default;
  MemoryInfoTest(const MemoryInfoTest&) = delete;
  MemoryInfoTest& operator=(const MemoryInfoTest&) = delete;

  const base::FilePath& root_dir() { return mock_context_.root_dir(); }

 private:
  MockContext mock_context_;
};

TEST_F(MemoryInfoTest, MeminfoSuccess) {
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      root_dir().Append(kRelativeMeminfoPath), kFakeMeminfoContents));

  auto memory_info = MemoryInfo::ParseFrom(root_dir());
  EXPECT_TRUE(memory_info.has_value());
  EXPECT_EQ(memory_info.value().total_memory_kib, kFakeMeminfoMemTotal);
  EXPECT_EQ(memory_info.value().free_memory_kib, kFakeMeminfonMemFree);
  EXPECT_EQ(memory_info.value().available_memory_kib,
            kFakeMeminfonMemAvailable);
}

TEST_F(MemoryInfoTest, MeminfoNoFile) {
  auto memory_info = MemoryInfo::ParseFrom(root_dir());
  EXPECT_FALSE(memory_info.has_value());
}

TEST_F(MemoryInfoTest, MeminfoFormattedIncorrectly) {
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      root_dir().Append(kRelativeMeminfoPath),
      kFakeMeminfoContentsIncorrectlyFormattedFile));

  auto memory_info = MemoryInfo::ParseFrom(root_dir());
  EXPECT_FALSE(memory_info.has_value());
}

TEST_F(MemoryInfoTest, MeminfoNoMemTotal) {
  ASSERT_TRUE(
      WriteFileAndCreateParentDirs(root_dir().Append(kRelativeMeminfoPath),
                                   kFakeMeminfoContentsMissingMemtotal));

  auto memory_info = MemoryInfo::ParseFrom(root_dir());
  EXPECT_FALSE(memory_info.has_value());
}

TEST_F(MemoryInfoTest, MeminfoNoMemFree) {
  ASSERT_TRUE(
      WriteFileAndCreateParentDirs(root_dir().Append(kRelativeMeminfoPath),
                                   kFakeMeminfoContentsMissingMemfree));

  auto memory_info = MemoryInfo::ParseFrom(root_dir());
  EXPECT_FALSE(memory_info.has_value());
}

TEST_F(MemoryInfoTest, MeminfoNoMemAvailable) {
  ASSERT_TRUE(
      WriteFileAndCreateParentDirs(root_dir().Append(kRelativeMeminfoPath),
                                   kFakeMeminfoContentsMissingMemavailable));

  auto memory_info = MemoryInfo::ParseFrom(root_dir());
  EXPECT_FALSE(memory_info.has_value());
}

TEST_F(MemoryInfoTest, MeminfoIncorrectlyFormattedMemTotal) {
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      root_dir().Append(kRelativeMeminfoPath),
      kFakeMeminfoContentsIncorrectlyFormattedMemtotal));

  auto memory_info = MemoryInfo::ParseFrom(root_dir());
  EXPECT_FALSE(memory_info.has_value());
}

TEST_F(MemoryInfoTest, MeminfoIncorrectlyFormattedMemFree) {
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      root_dir().Append(kRelativeMeminfoPath),
      kFakeMeminfoContentsIncorrectlyFormattedMemfree));

  auto memory_info = MemoryInfo::ParseFrom(root_dir());
  EXPECT_FALSE(memory_info.has_value());
}

TEST_F(MemoryInfoTest, MeminfoIncorrectlyFormattedMemAvailable) {
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      root_dir().Append(kRelativeMeminfoPath),
      kFakeMeminfoContentsIncorrectlyFormattedMemavailable));

  auto memory_info = MemoryInfo::ParseFrom(root_dir());
  EXPECT_FALSE(memory_info.has_value());
}

}  // namespace
}  // namespace diagnostics
