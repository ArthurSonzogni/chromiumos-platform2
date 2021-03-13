// Copyright (c) 2010 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bootstat/bootstat.h"

#include <errno.h>
#include <stdlib.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <memory>
#include <string>

#include <base/files/file_path.h>
#include <base/files/file_util.h>

#include <gtest/gtest.h>

namespace bootstat {

// TODO(drinkcat): Remove std::string
using std::string;

// Mock class to interact with the system.
class MockBootStatSystem : public BootStatSystem {
 public:
  explicit MockBootStatSystem(const base::FilePath& disk_statistics_file_path)
      : disk_statistics_file_path_(disk_statistics_file_path) {}

  base::FilePath GetDiskStatisticsFilePath() const override {
    return disk_statistics_file_path_;
  }

 private:
  base::FilePath disk_statistics_file_path_;
};

// TODO(drinkcat): Use anonymous namespace instead of static functions.
static void RemoveFile(const base::FilePath& file_path) {
  // Either this is a link, or the path exists (PathExists would resolve
  // symlink).
  EXPECT_TRUE(base::IsLink(file_path) || base::PathExists(file_path))
      << "Path does not exist " << file_path;
  EXPECT_TRUE(base::DeleteFile(file_path)) << "Cannot delete " << file_path;
}

// Class to track and test the data associated with a single event.
// The primary function is TestLogEvent():  This method wraps calls
// to bootstat_log() with code to track the expected contents of the
// event files.  After logging, the expected content is tested
// against the actual content.
class EventTracker {
 public:
  EventTracker(const string& name,
               const base::FilePath& uptime_prefix,
               const base::FilePath& disk_prefix);
  void TestLogEvent(const BootStat& bootstat,
                    const string& uptime,
                    const string& diskstats);
  void TestLogSymlink(const BootStat& bootstat,
                      const base::FilePath& dir_path,
                      bool create_target);
  void Reset();

 private:
  string event_name_;
  base::FilePath uptime_file_path_;
  string uptime_content_;
  base::FilePath diskstats_file_path_;
  string diskstats_content_;
};

EventTracker::EventTracker(const string& name,
                           const base::FilePath& uptime_prefix,
                           const base::FilePath& diskstats_prefix)
    : event_name_(name), uptime_content_(), diskstats_content_() {
  string truncated_name = event_name_.substr(0, BOOTSTAT_MAX_EVENT_LEN - 1);
  uptime_file_path_ = uptime_prefix.InsertBeforeExtension(truncated_name);
  diskstats_file_path_ = diskstats_prefix.InsertBeforeExtension(truncated_name);
}

// Basic helper function to test whether the contents of the
// specified file exactly match the given contents string.
static void ValidateEventFileContents(const base::FilePath& file_path,
                                      const string& expected_content) {
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

// Call bootstat_log() once, and update the expected content for
// this event.  Test that the new content of the event's files
// matches the updated expected content.
void EventTracker::TestLogEvent(const BootStat& bootstat,
                                const string& uptime,
                                const string& diskstats) {
  bootstat.LogEvent(event_name_.c_str());
  uptime_content_ += uptime;
  diskstats_content_ += diskstats;
  ValidateEventFileContents(uptime_file_path_, uptime_content_);
  ValidateEventFileContents(diskstats_file_path_, diskstats_content_);
}

static void TestSymlinkTarget(const base::FilePath& file_path,
                              bool expect_exists) {
  string data;
  bool ret = base::ReadFileToString(file_path, &data);
  if (expect_exists) {
    EXPECT_TRUE(ret) << "TestSymlinkTarget ReadFileToString(): " << file_path
                     << ": " << strerror(errno) << ".";
    EXPECT_TRUE(data.empty())
        << "TestSymlinkTarget read(): nbytes = " << data.size() << ".";
  } else {
    EXPECT_FALSE(ret) << "TestSymlinkTarget ReadFileToString(): " << file_path
                      << ": success was not expected";
  }
}

// Test calling bootstat_log() when the event files are symlinks.
// Calls to log events in this case are expected to produce no
// change in the file system.
//
// The test creates the necessary symlinks for the events, and
// optionally creates targets for the files.
void EventTracker::TestLogSymlink(const BootStat& bootstat,
                                  const base::FilePath& dir_path,
                                  bool create_target) {
  base::FilePath uptime_link_path("uptime.symlink");
  base::FilePath diskstats_link_path("disk.symlink");

  ASSERT_TRUE(base::CreateSymbolicLink(uptime_link_path, uptime_file_path_));
  ASSERT_TRUE(
      base::CreateSymbolicLink(diskstats_link_path, diskstats_file_path_));
  if (create_target) {
    ASSERT_TRUE(base::WriteFile(uptime_file_path_, ""));
    ASSERT_TRUE(base::WriteFile(diskstats_file_path_, ""));
  }

  bootstat.LogEvent(event_name_.c_str());

  TestSymlinkTarget(uptime_file_path_, create_target);
  TestSymlinkTarget(diskstats_file_path_, create_target);

  if (create_target) {
    RemoveFile(dir_path.Append(uptime_link_path));
    RemoveFile(dir_path.Append(diskstats_link_path));
  }
}

// Reset event state back to initial conditions, by deleting the
// associated event files, and clearing the expected contents.
void EventTracker::Reset() {
  uptime_content_.clear();
  diskstats_content_.clear();
  RemoveFile(diskstats_file_path_);
  RemoveFile(uptime_file_path_);
}

// Bootstat test class.  We use this class to override the
// dependencies in bootstat_log() on the file paths for /proc/uptime
// and /sys/block/<device>/stat.
//
// The class uses test-specific interfaces that change the default
// paths from the kernel statistics psuedo-files to temporary paths
// selected by this test.  This class also redirects the location for
// the event files created by bootstat_log() to a temporary directory.
class BootstatTest : public ::testing::Test {
 protected:
  virtual void SetUp();
  virtual void TearDown();

  EventTracker MakeEvent(const string& event_name) {
    return EventTracker(event_name, uptime_event_prefix_, disk_event_prefix_);
  }

  void SetMockStats(const char* uptime_content, const char* disk_content);
  void ClearMockStats();
  void TestLogEvent(EventTracker* event);
  void TestLogSymlink(EventTracker* event, bool create_target);

 private:
  base::FilePath stats_output_dir_;
  std::unique_ptr<BootStat> boot_stat_;
  // Raw pointer, owned by boot_stat_.
  MockBootStatSystem* boot_stat_system_;

  base::FilePath uptime_event_prefix_;
  base::FilePath disk_event_prefix_;

  // TODO(drinkcat): Replace mock_uptime_* with mock functions.
  string mock_uptime_file_name_;
  string mock_uptime_content_;
  base::FilePath mock_disk_file_path_;
  string mock_disk_content_;
};

void BootstatTest::SetUp() {
  // TODO(drinkcat): Use base::ScopedTempDir.
  ASSERT_TRUE(base::CreateTemporaryDirInDir(
      base::FilePath(""), "bootstat_test_", &stats_output_dir_))
      << "Cannot create temporary directory for tests.";
  uptime_event_prefix_ = stats_output_dir_.Append("uptime-");
  disk_event_prefix_ = stats_output_dir_.Append("disk-");
  mock_uptime_file_name_ = stats_output_dir_.Append("proc_uptime").value();
  mock_disk_file_path_ = stats_output_dir_.Append("block_stats");
  boot_stat_system_ = new MockBootStatSystem(mock_disk_file_path_);
  boot_stat_ = std::make_unique<BootStat>(
      stats_output_dir_, mock_uptime_file_name_,
      std::unique_ptr<BootStatSystem>(boot_stat_system_));
}

void BootstatTest::TearDown() {
  EXPECT_TRUE(base::DeleteFile(stats_output_dir_))
      << "BootstatTest::Teardown DeleteFile(): " << stats_output_dir_;
}

static void WriteMockStats(const string& content,
                           const base::FilePath& file_path) {
  ASSERT_TRUE(base::WriteFile(file_path, content))
      << "WriteMockStats WriteFile(): " << file_path;
}

// Set the content of the files mocking the contents of the kernel's
// statistics pseudo-files.  The strings provided here will be the
// ones recorded for subsequent calls to bootstat_log() for all
// events.
void BootstatTest::SetMockStats(const char* uptime_data,
                                const char* disk_data) {
  mock_uptime_content_ = string(uptime_data);
  WriteMockStats(mock_uptime_content_, base::FilePath(mock_uptime_file_name_));
  mock_disk_content_ = string(disk_data);
  WriteMockStats(mock_disk_content_, mock_disk_file_path_);
}

// Clean up the effects from SetMockStats().
void BootstatTest::ClearMockStats() {
  RemoveFile(base::FilePath(mock_uptime_file_name_));
  RemoveFile(mock_disk_file_path_);
}

void BootstatTest::TestLogEvent(EventTracker* event) {
  event->TestLogEvent(*boot_stat_, mock_uptime_content_, mock_disk_content_);
}

void BootstatTest::TestLogSymlink(EventTracker* event, bool create_target) {
  event->TestLogSymlink(*boot_stat_, base::FilePath(stats_output_dir_),
                        create_target);
}

// Test data to be used as input to SetMockStats().
//
// The structure of this array is pairs of strings, terminated by a
// single NULL.  The first string in the pair is content for
// /proc/uptime, the second for /sys/block/<device>/stat.
//
// This data is taken directly from a development system, and is
// representative of valid stats content, though not typical of what
// would be seen immediately after boot.
static const char* bootstat_data[] = {
    /*  0  */
    /* uptime */ "691448.42 11020440.26\n",
    /*  disk  */
    " 1417116    14896 55561564 10935990  4267850 78379879"
    " 661568738 1635920520      158 17856450 1649520570\n",
    /*  1  */
    /* uptime */ "691623.71 11021372.99\n",
    /*  disk  */
    " 1420714    14918 55689988 11006390  4287385 78594261"
    " 663441564 1651579200      152 17974280 1665255160\n",
    /* EOT */ nullptr};

// Tests that event file content matches expectations when an
// event is logged multiple times.
TEST_F(BootstatTest, ContentGeneration) {
  EventTracker ev = MakeEvent(string("test_event"));
  int i = 0;
  while (bootstat_data[i] != nullptr) {
    SetMockStats(bootstat_data[i], bootstat_data[i + 1]);
    TestLogEvent(&ev);
    i += 2;
  }
  ClearMockStats();
  ev.Reset();
}

// Tests that name truncation of logged events works as advertised.
TEST_F(BootstatTest, EventNameTruncation) {
  // clang-format off
  static const char kMostVoluminousEventName[] =
    //             16              32              48              64
    "event-6789abcdef_123456789ABCDEF.123456789abcdef0123456789abcdef"  //  64
    "=064+56789abcdef_123456789ABCDEF.123456789abcdef0123456789abcdef"  // 128
    "=128+56789abcdef_123456789ABCDEF.123456789abcdef0123456789abcdef"  // 191
    "=191+56789abcdef_123456789ABCDEF.123456789abcdef0123456789abcdef";  // 256
  // clang-format on

  string very_long(kMostVoluminousEventName);
  SetMockStats(bootstat_data[0], bootstat_data[1]);

  EventTracker ev = MakeEvent(very_long);
  TestLogEvent(&ev);
  ev.Reset();
  ev = MakeEvent(very_long.substr(0, 1));
  TestLogEvent(&ev);
  ev.Reset();
  ev = MakeEvent(very_long.substr(0, BOOTSTAT_MAX_EVENT_LEN - 1));
  TestLogEvent(&ev);
  ev.Reset();
  ev = MakeEvent(very_long.substr(0, BOOTSTAT_MAX_EVENT_LEN));
  TestLogEvent(&ev);
  ev.Reset();

  ClearMockStats();
}

// Test that event logging does not follow symbolic links.
TEST_F(BootstatTest, SymlinkFollow) {
  EventTracker ev = MakeEvent("symlink-no-follow");
  TestLogSymlink(&ev, true);
  ev.Reset();
  TestLogSymlink(&ev, false);
  ev.Reset();
}

}  // namespace bootstat
