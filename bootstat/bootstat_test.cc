// Copyright (c) 2010 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bootstat/bootstat.h"

#include <time.h>

#include <memory>
#include <set>
#include <string>

#include <base/files/file_enumerator.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/memory/ptr_util.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace bootstat {

using ::testing::_;
using ::testing::Mock;
using ::testing::Return;
using ::testing::SetArgPointee;

namespace {
void RemoveFile(const base::FilePath& file_path) {
  // Either this is a link, or the path exists (PathExists would resolve
  // symlink).
  EXPECT_TRUE(base::IsLink(file_path) || base::PathExists(file_path))
      << "Path does not exist " << file_path;
  EXPECT_TRUE(base::DeleteFile(file_path)) << "Cannot delete " << file_path;
}

// Basic helper function to test whether the contents of the
// specified file exactly match the given contents string.
void ValidateEventFileContents(const base::FilePath& file_path,
                               const base::StringPiece expected_content) {
  EXPECT_TRUE(base::PathIsWritable(file_path))
      << "ValidateEventFileContents access(): " << file_path
      << " is not writable: " << strerror(errno) << ".";
  ASSERT_TRUE(base::PathIsReadable(file_path))
      << "ValidateEventFileContents access(): " << file_path
      << " is not readable: " << strerror(errno) << ".";

  std::string actual_contents;
  ASSERT_TRUE(base::ReadFileToString(file_path, &actual_contents))
      << "ValidateEventFileContents cannot read " << file_path;
  EXPECT_EQ(expected_content, actual_contents)
      << "ValidateEventFileContents content mismatch.";
}
}  // namespace

// Mock class to interact with the system.
class MockBootStatSystem : public BootStatSystem {
 public:
  explicit MockBootStatSystem(const base::FilePath& disk_statistics_file_path)
      : disk_statistics_file_path_(disk_statistics_file_path) {}

  base::FilePath GetDiskStatisticsFilePath() const override {
    return disk_statistics_file_path_;
  }

  MOCK_METHOD(std::optional<struct timespec>, GetUpTime, (), (const, override));

 private:
  base::FilePath disk_statistics_file_path_;
};

// Test environment for Bootstat class.
// The class uses test-specific interfaces that change the default
// paths from the kernel statistics pseudo-files to temporary paths
// selected by this test.  This class also redirects the location for
// the event files created by BootStat.LogEvent() to a temporary directory.
class BootstatTest : public ::testing::Test {
 protected:
  virtual void SetUp();

  // Writes disk stats to mock file.
  bool WriteMockDiskStats(const std::string& content);

  // Checks that the stats directory only contains the expected files.
  void ValidateStatsDirectoryContent(const std::set<base::FilePath>& expected);

  base::ScopedTempDir temp_dir_;
  base::FilePath stats_output_dir_;
  std::unique_ptr<BootStat> boot_stat_;
  // Raw pointer, owned by boot_stat_.
  MockBootStatSystem* boot_stat_system_;

 private:
  base::FilePath mock_disk_file_path_;
};

void BootstatTest::SetUp() {
  ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
  stats_output_dir_ = temp_dir_.GetPath().Append("stats");
  ASSERT_TRUE(base::CreateDirectory(stats_output_dir_));
  mock_disk_file_path_ = temp_dir_.GetPath().Append("block_stats");
  boot_stat_system_ = new MockBootStatSystem(mock_disk_file_path_);
  boot_stat_ = std::make_unique<BootStat>(stats_output_dir_,
                                          base::WrapUnique(boot_stat_system_));
}

bool BootstatTest::WriteMockDiskStats(const std::string& content) {
  return base::WriteFile(mock_disk_file_path_, content);
}

void BootstatTest::ValidateStatsDirectoryContent(
    const std::set<base::FilePath>& expected) {
  std::set<base::FilePath> seen;

  base::FileEnumerator enumerator(
      stats_output_dir_, false,
      base::FileEnumerator::FILES | base::FileEnumerator::DIRECTORIES);
  for (base::FilePath name = enumerator.Next(); !name.empty();
       name = enumerator.Next())
    seen.insert(name);

  EXPECT_EQ(expected, seen);
}

// Holds LogEvent test data and expected results.
struct LogEventTestData {
  const struct timespec uptime;
  const char* expected_uptime;
  const char* mock_disk_content;
  const char* expected_disk_content;
};

constexpr struct LogEventTestData kDefaultTestData = {
    // uptime (tv_sec, tv_nsec)
    {691448, 123456789},
    // expected_uptime
    "691448.123456789\n",
    // mock_disk_content
    " 1417116    14896 55561564 10935990  4267850 78379879"
    " 661568738 1635920520      158 17856450 1649520570\n",
    // expected_disk_content
    " 1417116    14896 55561564 10935990  4267850 78379879"
    " 661568738 1635920520      158 17856450 1649520570\n",
};

// Tests that event file content matches expectations when an
// event is logged multiple times.
TEST_F(BootstatTest, ContentGeneration) {
  constexpr struct LogEventTestData kTestData[] = {
      {
          // uptime (tv_sec, tv_nsec)
          {691448, 123456789},
          // expected_uptime
          "691448.123456789\n",
          // mock_disk_content
          " 1417116    14896 55561564 10935990  4267850 78379879"
          " 661568738 1635920520      158 17856450 1649520570\n",
          // expected_disk_content
          " 1417116    14896 55561564 10935990  4267850 78379879"
          " 661568738 1635920520      158 17856450 1649520570\n",
      },
      {
          // uptime (tv_sec, tv_nsec)
          {691623, 12},  // Tests zero padding
                         // expected_uptime
          "691448.123456789\n691623.000000012\n",
          // mock_disk_content
          " 1420714    14918 55689988 11006390  4287385 78594261"
          " 663441564 1651579200      152 17974280 1665255160\n",
          // expected_disk_content
          " 1417116    14896 55561564 10935990  4267850 78379879"
          " 661568738 1635920520      158 17856450 1649520570\n"  // No
                                                                  // comma!
          " 1420714    14918 55689988 11006390  4287385 78594261"
          " 663441564 1651579200      152 17974280 1665255160\n",
      },
  };

  constexpr char kEventName[] = "test_event";
  base::FilePath uptime_file_path =
      stats_output_dir_.Append(std::string("uptime-") + kEventName);
  base::FilePath diskstats_file_path =
      stats_output_dir_.Append(std::string("disk-") + kEventName);

  for (int i = 0; i < std::size(kTestData); i++) {
    EXPECT_CALL(*boot_stat_system_, GetUpTime())
        .WillOnce(Return(std::make_optional(kTestData[i].uptime)));
    ASSERT_TRUE(WriteMockDiskStats(kTestData[i].mock_disk_content));

    boot_stat_->LogEvent(kEventName);

    Mock::VerifyAndClear(boot_stat_system_);

    ValidateEventFileContents(uptime_file_path, kTestData[i].expected_uptime);
    ValidateEventFileContents(diskstats_file_path,
                              kTestData[i].expected_disk_content);
    ValidateStatsDirectoryContent(
        std::set{uptime_file_path, diskstats_file_path});
  }
}

// Tests that name truncation of logged events works as advertised.
TEST_F(BootstatTest, EventNameTruncation) {
  constexpr struct {
    const char* event_name;
    const char* expected_event_name;
  } kTestData[] = {
      // clang-format off
  {
    //             16              32              48              64
    // kEventName: 256 chars
    "event-6789abcdef_123456789ABCDEF.123456789abcdef0123456789abcdef"
    "=064+56789abcdef_123456789ABCDEF.123456789abcdef0123456789abcdef"
    "=128+56789abcdef_123456789ABCDEF.123456789abcdef0123456789abcdef"
    "=191+56789abcdef_123456789ABCDEF.123456789abcdef0123456789abcdef",
    // expected_kEventName: 256 chars
    "event-6789abcdef_123456789ABCDEF.123456789abcdef0123456789abcde",
  },
  {
    "ev",  // kEventName: 2 chars
    "ev",  // expected_kEventName: 2 chars (not truncated)
  },
  {
    // kEventName: 64 chars
    "event-6789abcdef_123456789ABCDEF.123456789abcdef0123456789abcdef",
    // expected_kEventName: 63 chars
    "event-6789abcdef_123456789ABCDEF.123456789abcdef0123456789abcde",
  },
  {
    // kEventName: 63 chars
    "event-6789abcdef_123456789ABCDEF.123456789abcdef0123456789abcde",
    // expected_kEventName: 63 chars (not truncated)
    "event-6789abcdef_123456789ABCDEF.123456789abcdef0123456789abcde",
  },
      // clang-format on
  };

  for (int i = 0; i < std::size(kTestData); i++) {
    EXPECT_CALL(*boot_stat_system_, GetUpTime())
        .WillOnce(Return(std::make_optional(kDefaultTestData.uptime)));
    ASSERT_TRUE(WriteMockDiskStats(kDefaultTestData.mock_disk_content));

    boot_stat_->LogEvent(kTestData[i].event_name);

    Mock::VerifyAndClear(boot_stat_system_);

    base::FilePath uptime_file_path = stats_output_dir_.Append(
        std::string("uptime-") + kTestData[i].expected_event_name);
    base::FilePath diskstats_file_path = stats_output_dir_.Append(
        std::string("disk-") + kTestData[i].expected_event_name);
    ValidateEventFileContents(uptime_file_path,
                              kDefaultTestData.expected_uptime);
    ValidateEventFileContents(diskstats_file_path,
                              kDefaultTestData.mock_disk_content);
    ValidateStatsDirectoryContent(
        std::set{uptime_file_path, diskstats_file_path});
    RemoveFile(diskstats_file_path);
    RemoveFile(uptime_file_path);
  }
}

// Test that event logging does not follow symbolic links (even if the target
// exists).
TEST_F(BootstatTest, SymlinkFollowTarget) {
  constexpr char kEventName[] = "symlink-no-follow";
  base::FilePath uptime_file_path =
      stats_output_dir_.Append(std::string("uptime-") + kEventName);
  base::FilePath diskstats_file_path =
      stats_output_dir_.Append(std::string("disk-") + kEventName);

  EXPECT_CALL(*boot_stat_system_, GetUpTime())
      .WillRepeatedly(Return(std::make_optional(kDefaultTestData.uptime)));
  ASSERT_TRUE(WriteMockDiskStats(kDefaultTestData.mock_disk_content));

  // Relative targets for the symbolic links.
  base::FilePath uptime_link_path("uptime.symlink");
  base::FilePath diskstats_link_path("disk.symlink");

  ASSERT_TRUE(base::CreateSymbolicLink(uptime_link_path, uptime_file_path));
  ASSERT_TRUE(
      base::CreateSymbolicLink(diskstats_link_path, diskstats_file_path));

  // Create the symlink targets
  constexpr char kDefaultContent[] = "DEFAULT";
  ASSERT_TRUE(base::WriteFile(uptime_file_path, kDefaultContent));
  ASSERT_TRUE(base::WriteFile(diskstats_file_path, kDefaultContent));

  boot_stat_->LogEvent(kEventName);

  // Expect no additional content in the files.
  std::string data;
  EXPECT_TRUE(base::ReadFileToString(uptime_file_path, &data));
  EXPECT_EQ(data, kDefaultContent);
  EXPECT_TRUE(base::ReadFileToString(diskstats_file_path, &data));
  EXPECT_EQ(data, kDefaultContent);
}

// Test that event logging does not follow symbolic links (when the target does
// not exists).
TEST_F(BootstatTest, SymlinkFollowNoTarget) {
  constexpr char kEventName[] = "symlink-no-follow";
  base::FilePath uptime_file_path =
      stats_output_dir_.Append(std::string("uptime-") + kEventName);
  base::FilePath diskstats_file_path =
      stats_output_dir_.Append(std::string("disk-") + kEventName);

  EXPECT_CALL(*boot_stat_system_, GetUpTime())
      .WillRepeatedly(Return(std::make_optional(kDefaultTestData.uptime)));
  ASSERT_TRUE(WriteMockDiskStats(kDefaultTestData.mock_disk_content));

  // Relative targets for the symbolic links.
  base::FilePath uptime_link_path("uptime.symlink");
  base::FilePath diskstats_link_path("disk.symlink");

  ASSERT_TRUE(base::CreateSymbolicLink(uptime_link_path, uptime_file_path));
  ASSERT_TRUE(
      base::CreateSymbolicLink(diskstats_link_path, diskstats_file_path));

  boot_stat_->LogEvent(kEventName);

  // Expect to be unable to read content
  std::string data;
  EXPECT_FALSE(base::ReadFileToString(uptime_file_path, &data));
  EXPECT_FALSE(base::ReadFileToString(diskstats_file_path, &data));

  // ... and the targets must not exist
  EXPECT_FALSE(base::PathExists(stats_output_dir_.Append(uptime_link_path)));
  EXPECT_FALSE(base::PathExists(stats_output_dir_.Append(diskstats_link_path)));
}

}  // namespace bootstat
