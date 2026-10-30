// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "biod/updater/update_utils.h"

#include <unistd.h>

#include <string>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/logging.h>
#include <base/time/time.h>
#include <brillo/daemons/daemon.h>
#include <brillo/syslog_logging.h>
#include <gtest/gtest.h>

namespace biod {
namespace updater {
namespace {

constexpr char kLatestLogSymlink[] = "bio_fw_updater.LATEST";
constexpr char kSetupLoggingFailure[] = "Failed to set up log directory";
constexpr char kLogMarker[] = "SetupLoggingTest marker";

class SetupLoggingTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    // The test runner (and TearDown() below) leave brillo's log message
    // handler installed. Remove it so that only messages logged after
    // SetupLogging() calls brillo::InitLog() are captured by FindLog(), which
    // matches bio_fw_updater's main() before SetupLogging() is called.
    logging::SetLogMessageHandler(nullptr);
    brillo::ClearLog();
    brillo::LogToString(true);
  }

  void TearDown() override {
    // Release the log file opened by SetupLogging() and restore stderr logging
    // so that later tests are not affected.
    logging::CloseLogFile();
    brillo::InitLog(brillo::kLogToStderr);
    brillo::LogToString(false);
    brillo::ClearLog();
  }

  base::FilePath LatestLogPath() const {
    return temp_dir_.GetPath().Append(kLatestLogSymlink);
  }

  // Occupies the candidate log file names for the next few seconds with
  // directories, so that opening the log file fails regardless of privileges
  // while the log directory itself stays writable.
  void BlockLogFileNames() {
    const base::Time now = base::Time::Now();
    for (int i = 0; i < 3; ++i) {
      ASSERT_TRUE(base::CreateDirectory(temp_dir_.GetPath().Append(
          "bio_fw_updater." +
          brillo::GetTimeAsLogString(now + base::Seconds(i)))));
    }
  }

  base::ScopedTempDir temp_dir_;
};

TEST_F(SetupLoggingTest, ValidDirectoryInitializesFileLogging) {
  EXPECT_TRUE(SetupLogging(temp_dir_.GetPath()));
  EXPECT_TRUE(base::IsLink(LatestLogPath()));
  EXPECT_TRUE(base::PathExists(LatestLogPath()));
  EXPECT_FALSE(brillo::FindLog(kSetupLoggingFailure));

  // Messages must end up in the file behind bio_fw_updater.LATEST.
  LOG(INFO) << kLogMarker;
  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(LatestLogPath(), &contents));
  EXPECT_NE(contents.find(kLogMarker), std::string::npos);
}

TEST_F(SetupLoggingTest, NonExistentDirectoryFallsBackToSyslog) {
  const base::FilePath missing_dir = temp_dir_.GetPath().Append("nonexistent");

  EXPECT_FALSE(SetupLogging(missing_dir));
  EXPECT_TRUE(brillo::FindLog(kSetupLoggingFailure));
}

TEST_F(SetupLoggingTest, UnwritableDirectoryFallsBackToSyslog) {
  if (geteuid() == 0) {
    GTEST_SKIP() << "Directory permissions are not enforced for root.";
  }

  const base::FilePath read_only_dir = temp_dir_.GetPath().Append("readonly");
  ASSERT_TRUE(base::CreateDirectory(read_only_dir));
  ASSERT_TRUE(base::SetPosixFilePermissions(
      read_only_dir, base::FILE_PERMISSION_READ_BY_USER |
                         base::FILE_PERMISSION_EXECUTE_BY_USER));

  EXPECT_FALSE(SetupLogging(read_only_dir));
  EXPECT_FALSE(base::IsLink(read_only_dir.Append(kLatestLogSymlink)));
  EXPECT_TRUE(brillo::FindLog(kSetupLoggingFailure));
}

TEST_F(SetupLoggingTest, LogFileOpenFailureFallsBackToSyslog) {
  BlockLogFileNames();

  EXPECT_FALSE(SetupLogging(temp_dir_.GetPath()));
  // The directory is writable, so a symlink would have been created if the
  // symlinks were updated before file logging was initialized.
  EXPECT_FALSE(base::IsLink(LatestLogPath()));
  EXPECT_FALSE(base::PathExists(LatestLogPath()));
  EXPECT_TRUE(brillo::FindLog(kSetupLoggingFailure));
}

}  // namespace
}  // namespace updater
}  // namespace biod
