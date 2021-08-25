// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/files/file_path.h>
#include <gtest/gtest.h>

#include "diagnostics/common/file_test_utils.h"
#include "diagnostics/cros_healthd/fetchers/memory_fetcher.h"
#include "diagnostics/cros_healthd/system/mock_context.h"

namespace diagnostics {

namespace {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

constexpr char kRelativeMeminfoPath[] = "proc/meminfo";
constexpr char kRelativeVmStatPath[] = "proc/vmstat";

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

constexpr char kFakeVmStatContents[] = "foo 98\npgfault 654654\n";
constexpr char kFakeVmStatContentsIncorrectlyFormattedFile[] =
    "NoKey\npgfault 71023\n";
constexpr char kFakeVmStatContentsMissingPgfault[] = "foo 9908\n";
constexpr char kFakeVmStatContentsIncorrectlyFormattedPgfault[] =
    "pgfault NotAnInteger\n";

class MemoryFetcherTest : public ::testing::Test {
 protected:
  MemoryFetcherTest() = default;

  const base::FilePath& root_dir() { return mock_context_.root_dir(); }

  mojo_ipc::MemoryResultPtr FetchMemoryInfo() {
    return memory_fetcher_.FetchMemoryInfo();
  }

 private:
  MockContext mock_context_;
  MemoryFetcher memory_fetcher_{&mock_context_};
};

// Test that memory info can be read when it exists.
TEST_F(MemoryFetcherTest, TestFetchMemoryInfo) {
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      root_dir().Append(kRelativeMeminfoPath), kFakeMeminfoContents));
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      root_dir().Append(kRelativeVmStatPath), kFakeVmStatContents));

  auto result = FetchMemoryInfo();
  ASSERT_TRUE(result->is_memory_info());

  const auto& info = result->get_memory_info();
  EXPECT_EQ(info->total_memory_kib, 3906320);
  EXPECT_EQ(info->free_memory_kib, 873180);
  EXPECT_EQ(info->available_memory_kib, 87980);
  EXPECT_EQ(info->page_faults_since_last_boot, 654654);
}

// Test that fetching memory info returns an error when /proc/meminfo doesn't
// exist.
TEST_F(MemoryFetcherTest, TestFetchMemoryInfoNoProcMeminfo) {
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      root_dir().Append(kRelativeVmStatPath), kFakeVmStatContents));

  auto result = FetchMemoryInfo();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->type, mojo_ipc::ErrorType::kFileReadError);
}

// Test that fetching memory info returns an error when /proc/meminfo is
// formatted incorrectly.
TEST_F(MemoryFetcherTest, TestFetchMemoryInfoProcMeminfoFormattedIncorrectly) {
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      root_dir().Append(kRelativeMeminfoPath),
      kFakeMeminfoContentsIncorrectlyFormattedFile));

  auto result = FetchMemoryInfo();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->type, mojo_ipc::ErrorType::kParseError);
}

// Test that fetching memory info returns an error when /proc/meminfo doesn't
// contain the MemTotal key.
TEST_F(MemoryFetcherTest, TestFetchMemoryInfoProcMeminfoNoMemTotal) {
  ASSERT_TRUE(
      WriteFileAndCreateParentDirs(root_dir().Append(kRelativeMeminfoPath),
                                   kFakeMeminfoContentsMissingMemtotal));
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      root_dir().Append(kRelativeVmStatPath), kFakeVmStatContents));

  auto result = FetchMemoryInfo();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->type, mojo_ipc::ErrorType::kParseError);
}

// Test that fetching memory info returns an error when /proc/meminfo doesn't
// contain the MemFree key.
TEST_F(MemoryFetcherTest, TestFetchMemoryInfoProcMeminfoNoMemFree) {
  ASSERT_TRUE(
      WriteFileAndCreateParentDirs(root_dir().Append(kRelativeMeminfoPath),
                                   kFakeMeminfoContentsMissingMemfree));
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      root_dir().Append(kRelativeVmStatPath), kFakeVmStatContents));

  auto result = FetchMemoryInfo();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->type, mojo_ipc::ErrorType::kParseError);
}

// Test that fetching memory info returns an error when /proc/meminfo doesn't
// contain the MemAvailable key.
TEST_F(MemoryFetcherTest, TestFetchMemoryInfoProcMeminfoNoMemAvailable) {
  ASSERT_TRUE(
      WriteFileAndCreateParentDirs(root_dir().Append(kRelativeMeminfoPath),
                                   kFakeMeminfoContentsMissingMemavailable));
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      root_dir().Append(kRelativeVmStatPath), kFakeVmStatContents));

  auto result = FetchMemoryInfo();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->type, mojo_ipc::ErrorType::kParseError);
}

// Test that fetching memory info returns an error when /proc/meminfo contains
// an incorrectly formatted MemTotal key.
TEST_F(MemoryFetcherTest,
       TestFetchMemoryInfoProcMeminfoIncorrectlyFormattedMemTotal) {
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      root_dir().Append(kRelativeMeminfoPath),
      kFakeMeminfoContentsIncorrectlyFormattedMemtotal));
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      root_dir().Append(kRelativeVmStatPath), kFakeVmStatContents));

  auto result = FetchMemoryInfo();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->type, mojo_ipc::ErrorType::kParseError);
}

// Test that fetching memory info returns an error when /proc/meminfo contains
// an incorrectly formatted MemFree key.
TEST_F(MemoryFetcherTest,
       TestFetchMemoryInfoProcMeminfoIncorrectlyFormattedMemFree) {
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      root_dir().Append(kRelativeMeminfoPath),
      kFakeMeminfoContentsIncorrectlyFormattedMemfree));
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      root_dir().Append(kRelativeVmStatPath), kFakeVmStatContents));

  auto result = FetchMemoryInfo();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->type, mojo_ipc::ErrorType::kParseError);
}

// Test that fetching memory info returns an error when /proc/meminfo contains
// an incorrectly formatted MemAvailable key.
TEST_F(MemoryFetcherTest,
       TestFetchMemoryInfoProcMeminfoIncorrectlyFormattedMemAvailable) {
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      root_dir().Append(kRelativeMeminfoPath),
      kFakeMeminfoContentsIncorrectlyFormattedMemavailable));
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      root_dir().Append(kRelativeVmStatPath), kFakeVmStatContents));

  auto result = FetchMemoryInfo();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->type, mojo_ipc::ErrorType::kParseError);
}

// Test that fetching memory info returns an error when /proc/vmstat doesn't
// exist.
TEST_F(MemoryFetcherTest, TestFetchMemoryInfoNoProcVmStat) {
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      root_dir().Append(kRelativeMeminfoPath), kFakeMeminfoContents));

  auto result = FetchMemoryInfo();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->type, mojo_ipc::ErrorType::kFileReadError);
}

// Test that fetching memory info returns an error when /proc/vmstat is
// formatted incorrectly.
TEST_F(MemoryFetcherTest, TestFetchMemoryInfoProcVmStatFormattedIncorrectly) {
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      root_dir().Append(kRelativeMeminfoPath), kFakeMeminfoContents));
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      root_dir().Append(kRelativeVmStatPath),
      kFakeVmStatContentsIncorrectlyFormattedFile));

  auto result = FetchMemoryInfo();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->type, mojo_ipc::ErrorType::kParseError);
}

// Test that fetching memory info returns an error when /proc/vmstat doesn't
// contain the pgfault key.
TEST_F(MemoryFetcherTest, TestFetchMemoryInfoProcVmStatNoPgfault) {
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      root_dir().Append(kRelativeMeminfoPath), kFakeMeminfoContents));
  ASSERT_TRUE(
      WriteFileAndCreateParentDirs(root_dir().Append(kRelativeVmStatPath),
                                   kFakeVmStatContentsMissingPgfault));

  auto result = FetchMemoryInfo();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->type, mojo_ipc::ErrorType::kParseError);
}

// Test that fetching memory info returns an error when /proc/vmstat contains
// an incorrectly formatted pgfault key.
TEST_F(MemoryFetcherTest,
       TestFetchMemoryInfoProcVmStatIncorrectlyFormattedPgfault) {
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      root_dir().Append(kRelativeMeminfoPath), kFakeMeminfoContents));
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      root_dir().Append(kRelativeVmStatPath),
      kFakeVmStatContentsIncorrectlyFormattedPgfault));

  auto result = FetchMemoryInfo();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->type, mojo_ipc::ErrorType::kParseError);
}

}  // namespace

}  // namespace diagnostics
