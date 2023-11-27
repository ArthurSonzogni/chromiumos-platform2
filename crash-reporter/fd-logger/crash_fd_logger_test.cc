// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/fd-logger/crash_fd_logger.h"

#include <array>
#include <string>

#include <unistd.h>

#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/files/scoped_temp_dir.h>
#include <base/logging.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

using ::testing::HasSubstr;

namespace {

const size_t kMaxFiles = 16;
constexpr std::array<int, 4> kLinksToCreate = {2, 10, 5, 1};

}  // namespace

namespace fd_logger {

TEST(CrashFdLoggerTest, LogOpenFilesInSystem) {
  // Log into a file so we can verify the output.
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath file_log_path = temp_dir.GetPath().Append("output.log");
  base::ScopedFILE log_file(base::OpenFile(file_log_path, "w"));

  logging::LoggingSettings logging_settings;
  logging_settings.logging_dest = logging::LOG_TO_FILE;
  logging_settings.log_file = log_file.get();
  logging::InitLogging(logging_settings);

  // Create a simple version of /proc with a fake process that has opened a
  // bunch of files.
  base::ScopedTempDir proc_dir;
  ASSERT_TRUE(proc_dir.CreateUniqueTempDir());
  base::FilePath pid_path = proc_dir.GetPath().Append("123");
  ASSERT_TRUE(base::CreateDirectory(pid_path));
  base::FilePath fd_path = pid_path.Append("fd");
  ASSERT_TRUE(base::CreateDirectory(fd_path));

  base::FilePath exe_path = pid_path.Append("exe");
  ASSERT_EQ(symlink("/bin/fake_process", exe_path.MaybeAsASCII().c_str()), 0);

  // Create a several sets of kMaxFiles, with each set having
  // kLinksToCreate[set_num] of links to it. We should see the files
  // that have the most links reported, and those with fewest truncated from
  // the log output.
  size_t link_num = 0;
  for (size_t i = 0; i < kLinksToCreate.size(); i++) {
    for (size_t link_count = 0; link_count < kLinksToCreate[i]; link_count++) {
      for (size_t file_count = 0; file_count < kMaxFiles; file_count++) {
        size_t file_num = file_count + kMaxFiles * i;
        base::FilePath link_path = fd_path.Append(std::to_string(link_num++));
        std::string link_target =
            std::string("file") + std::to_string(file_num);
        ASSERT_EQ(
            symlink(link_target.c_str(), link_path.MaybeAsASCII().c_str()), 0);
      }
    }
  }

  base::FilePath fs_dir = proc_dir.GetPath().Append("sys").Append("fs");
  ASSERT_TRUE(base::CreateDirectory(fs_dir));
  ASSERT_TRUE(base::WriteFile(fs_dir.Append("file-nr"), "26352\t0\t1048576\n"));

  LogOpenFilesInSystem(proc_dir.GetPath());

  std::string output;
  ASSERT_TRUE(base::ReadFileToString(file_log_path, &output));
  EXPECT_THAT(output, HasSubstr("exe=/bin/fake_process"));
  EXPECT_THAT(output, HasSubstr("count=288"));
  EXPECT_THAT(output,
              HasSubstr("open_counts=10,10,10,10,10,10,10,10,10,10,10,10,10,10,"
                        "10,10,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5\n"));
  EXPECT_THAT(output, HasSubstr(", open: 26352, max: 1048576"));
}

}  // namespace fd_logger
