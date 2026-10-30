// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "biod/updater/update_utils.h"

#include <sys/stat.h>
#include <unistd.h>

#include <optional>
#include <string>
#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/files/scoped_temp_dir.h>
#include <base/logging.h>
#include <base/time/time.h>
#include <brillo/daemons/daemon.h>
#include <brillo/files/file_util.h>
#include <brillo/files/safe_fd.h>
#include <brillo/syslog_logging.h>
#include <gtest/gtest.h>

namespace biod {
namespace updater {
namespace {

constexpr char kLatestLogSymlink[] = "bio_fw_updater.LATEST";
constexpr char kSetupLoggingFailure[] = "Failed to set up log directory";
constexpr char kLogMarker[] = "SetupLoggingTest marker";
constexpr char kLogFileName[] = "bio_fw_updater.log";
constexpr mode_t kLogFilePermissions = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;

mode_t GetMode(const brillo::SafeFD& fd) {
  struct stat st;
  EXPECT_EQ(fstat(fd.get(), &st), 0);
  EXPECT_TRUE(S_ISREG(st.st_mode));
  return st.st_mode & ALLPERMS;
}

class SafeLogFileTest : public ::testing::Test {
 protected:
  void SetUp() override { ASSERT_TRUE(temp_dir_.CreateUniqueTempDir()); }

  base::ScopedTempDir temp_dir_;
};

TEST_F(SafeLogFileTest, OpenDirectorySafelyRejectsMissingFileAndSymlink) {
  const base::FilePath log_dir = temp_dir_.GetPath().Append("log");
  EXPECT_FALSE(OpenDirectorySafely(log_dir).has_value());

  ASSERT_TRUE(base::WriteFile(log_dir, ""));
  EXPECT_FALSE(OpenDirectorySafely(log_dir).has_value());
  ASSERT_TRUE(brillo::DeleteFile(log_dir));

  ASSERT_TRUE(base::CreateDirectory(log_dir));
  EXPECT_TRUE(OpenDirectorySafely(log_dir).has_value());

  const base::FilePath symlink_dir = temp_dir_.GetPath().Append("symlink_log");
  ASSERT_TRUE(base::CreateSymbolicLink(log_dir, symlink_dir));
  EXPECT_FALSE(OpenDirectorySafely(symlink_dir).has_value());
}

TEST_F(SafeLogFileTest, OpenFileForWritingSafelyCreatesRegularFile) {
  std::optional<brillo::SafeFD> dir_fd =
      OpenDirectorySafely(temp_dir_.GetPath());
  ASSERT_TRUE(dir_fd.has_value());

  std::optional<brillo::SafeFD> file_fd =
      OpenFileForWritingSafely(*dir_fd, kLogFileName, kLogFilePermissions);
  ASSERT_TRUE(file_fd.has_value());
  EXPECT_EQ(GetMode(*file_fd), kLogFilePermissions);
  EXPECT_TRUE(base::PathExists(temp_dir_.GetPath().Append(kLogFileName)));
}

TEST_F(SafeLogFileTest, OpenFileForWritingSafelyReplacesSymlink) {
  const base::FilePath target_path = temp_dir_.GetPath().Append("target");
  const base::FilePath log_path = temp_dir_.GetPath().Append(kLogFileName);
  ASSERT_TRUE(base::WriteFile(target_path, "original"));
  ASSERT_TRUE(base::CreateSymbolicLink(target_path, log_path));

  std::optional<brillo::SafeFD> dir_fd =
      OpenDirectorySafely(temp_dir_.GetPath());
  ASSERT_TRUE(dir_fd.has_value());
  std::optional<brillo::SafeFD> file_fd =
      OpenFileForWritingSafely(*dir_fd, kLogFileName, kLogFilePermissions);
  ASSERT_TRUE(file_fd.has_value());

  // The symlink is replaced by a regular file and its target is untouched.
  EXPECT_FALSE(base::IsLink(log_path));
  EXPECT_EQ(GetMode(*file_fd), kLogFilePermissions);
  std::string target_contents;
  ASSERT_TRUE(base::ReadFileToString(target_path, &target_contents));
  EXPECT_EQ(target_contents, "original");
}

TEST_F(SafeLogFileTest, OpenFileForWritingSafelyRestoresPermissions) {
  std::optional<brillo::SafeFD> dir_fd =
      OpenDirectorySafely(temp_dir_.GetPath());
  ASSERT_TRUE(dir_fd.has_value());
  std::optional<brillo::SafeFD> file_fd =
      OpenFileForWritingSafely(*dir_fd, kLogFileName, kLogFilePermissions);
  ASSERT_TRUE(file_fd.has_value());

  // Mode bits outside 0777 are not validated by SafeFD, so the sticky bit
  // exercises the explicit fchmod() in OpenFileForWritingSafely().
  ASSERT_EQ(fchmod(file_fd->get(), kLogFilePermissions | S_ISVTX), 0);
  file_fd.reset();

  file_fd =
      OpenFileForWritingSafely(*dir_fd, kLogFileName, kLogFilePermissions);
  ASSERT_TRUE(file_fd.has_value());
  EXPECT_EQ(GetMode(*file_fd), kLogFilePermissions);
}

TEST_F(SafeLogFileTest, SafeFDToScopedFILERejectsInvalidFD) {
  EXPECT_FALSE(SafeFDToScopedFILE(brillo::SafeFD(), "a"));
}

TEST_F(SafeLogFileTest, SafeFDToScopedFILEWritesToFile) {
  std::optional<brillo::SafeFD> dir_fd =
      OpenDirectorySafely(temp_dir_.GetPath());
  ASSERT_TRUE(dir_fd.has_value());
  std::optional<brillo::SafeFD> file_fd =
      OpenFileForWritingSafely(*dir_fd, kLogFileName, kLogFilePermissions);
  ASSERT_TRUE(file_fd.has_value());

  base::ScopedFILE stream = SafeFDToScopedFILE(std::move(*file_fd), "a");
  ASSERT_TRUE(stream);
  ASSERT_GE(fputs("hello\n", stream.get()), 0);
  stream.reset();

  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(temp_dir_.GetPath().Append(kLogFileName),
                                     &contents));
  EXPECT_EQ(contents, "hello\n");
}

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

  // Occupies the candidate log file names for the next few seconds with FIFOs,
  // which OpenFileForWritingSafely() can neither open for writing (no reader)
  // nor replace, so that opening the log file fails regardless of privileges
  // while the log directory itself stays writable.
  void BlockLogFileNames() {
    const base::Time now = base::Time::Now();
    for (int i = 0; i < 3; ++i) {
      const base::FilePath fifo = temp_dir_.GetPath().Append(
          "bio_fw_updater." +
          brillo::GetTimeAsLogString(now + base::Seconds(i)));
      ASSERT_EQ(mkfifo(fifo.value().c_str(), S_IRUSR | S_IWUSR), 0);
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
