// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "debugd/src/helpers/top_io_threads_utils.h"

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <brillo/file_utils.h>
#include <gtest/gtest.h>

class TopIoThreadsUtilsTest : public ::testing::Test {
 public:
  base::FilePath GetTestRootDir() { return test_root_dir_.GetPath(); }

 protected:
  void SetUp() override { ASSERT_TRUE(test_root_dir_.CreateUniqueTempDir()); }

 private:
  base::ScopedTempDir test_root_dir_;
};

TEST_F(TopIoThreadsUtilsTest, TestWithTwoProcessesAndThreeThreads) {
  // Process #1 that includes two threads.
  base::FilePath pid1_path = GetTestRootDir().Append("1");
  base::FilePath pid1_task_path = pid1_path.Append("task");

  // Thread #1 in process #1.
  base::FilePath tid1_path = pid1_task_path.Append("1");
  base::FilePath tid1_comm_path = tid1_path.Append("comm");
  base::FilePath tid1_io_path = tid1_path.Append("io");

  brillo::WriteStringToFile(tid1_comm_path, "123\n");
  std::string io_content1 =
      "rchar: 1131213\nwchar: 7819\nsyscr: 1787\nsyscw: 389\nread_bytes: "
      "114688\nwrite_bytes: 56\ncancelled_write_bytes: 0\n";
  brillo::WriteStringToFile(tid1_io_path, io_content1);

  // Thread #2 in process #1.
  base::FilePath tid2_path = pid1_task_path.Append("2");
  base::FilePath tid2_comm_path = tid2_path.Append("comm");
  base::FilePath tid2_io_path = tid2_path.Append("io");

  brillo::WriteStringToFile(tid2_comm_path, "456\n");
  std::string io_content2 =
      "rchar: 1131301\nwchar: 8063\nsyscr: 1841\nsyscw: 396\nread_bytes: "
      "224688\nwrite_bytes: 66\ncancelled_write_bytes: 0\n";
  brillo::WriteStringToFile(tid2_io_path, io_content2);

  // Process #2 which includes a single thread.
  base::FilePath pid2_path = GetTestRootDir().Append("2");
  base::FilePath pid2_task_path = pid2_path.Append("task");

  base::FilePath tid12_path = pid2_task_path.Append("12");
  base::FilePath tid12_comm_path = tid12_path.Append("comm");
  base::FilePath tid12_io_path = tid12_path.Append("io");

  brillo::WriteStringToFile(tid12_comm_path, "789\n");
  std::string io_content12 =
      "rchar: 1131301\nwchar: 8063\nsyscr: 1841\nsyscw: 396\nread_bytes: "
      "88\nwrite_bytes: 96\ncancelled_write_bytes: 0\n";
  brillo::WriteStringToFile(tid12_io_path, io_content12);

  std::vector<debugd::thread_io_stats> stats;
  debugd::LoadThreadIoStats(GetTestRootDir(), stats, 10);

  EXPECT_EQ(stats.size(), 3);

  EXPECT_EQ(stats[2].pid, 1);
  EXPECT_EQ(stats[2].tid, 2);
  EXPECT_EQ(stats[2].command, "456");
  EXPECT_EQ(stats[2].bytes_read, 224688);
  EXPECT_EQ(stats[2].bytes_written, 66);

  EXPECT_EQ(stats[1].pid, 1);
  EXPECT_EQ(stats[1].tid, 1);
  EXPECT_EQ(stats[1].command, "123");
  EXPECT_EQ(stats[1].bytes_read, 114688);
  EXPECT_EQ(stats[1].bytes_written, 56);

  EXPECT_EQ(stats[0].pid, 2);
  EXPECT_EQ(stats[0].tid, 12);
  EXPECT_EQ(stats[0].command, "789");
  EXPECT_EQ(stats[0].bytes_read, 88);
  EXPECT_EQ(stats[0].bytes_written, 96);
}

TEST_F(TopIoThreadsUtilsTest, TestWithNonexistentProcDirectory) {
  base::FilePath nonexistent_path = GetTestRootDir().Append("nonexistent");

  std::vector<debugd::thread_io_stats> stats;
  debugd::LoadThreadIoStats(nonexistent_path, stats, 10);

  EXPECT_EQ(stats.size(), 0);
}

TEST_F(TopIoThreadsUtilsTest, TestWithEmptyProcDirectory) {
  // Not a single process to be found.

  std::vector<debugd::thread_io_stats> stats;
  debugd::LoadThreadIoStats(GetTestRootDir(), stats, 10);

  EXPECT_EQ(stats.size(), 0);
}

TEST_F(TopIoThreadsUtilsTest, TestSingleProcessWithInaccessibleProcDirectory) {
  base::FilePath pid1_path = GetTestRootDir().Append("1");

  ASSERT_TRUE(base::CreateDirectory(pid1_path));

  ASSERT_TRUE(base::SetPosixFilePermissions(GetTestRootDir(), 0000));

  std::vector<debugd::thread_io_stats> stats;
  debugd::LoadThreadIoStats(GetTestRootDir(), stats, 10);

  EXPECT_EQ(stats.size(), 0);
}

TEST_F(TopIoThreadsUtilsTest, TestSingleProcessWithNoTaskDirectory) {
  // Process #1 that has no associated task directory.
  base::FilePath pid1_path = GetTestRootDir().Append("1");

  ASSERT_TRUE(base::CreateDirectory(pid1_path));

  std::vector<debugd::thread_io_stats> stats;
  debugd::LoadThreadIoStats(GetTestRootDir(), stats, 10);

  EXPECT_EQ(stats.size(), 0);
}

TEST_F(TopIoThreadsUtilsTest,
       TestSingleProcessWithInaccessibleProcessDirectory) {
  // Process #1 that includes a single thread
  base::FilePath pid1_path = GetTestRootDir().Append("1");
  base::FilePath pid1_task_path = pid1_path.Append("task");

  base::FilePath tid1_path = pid1_task_path.Append("1");
  base::FilePath tid1_io_path = tid1_path.Append("io");
  base::FilePath tid1_comm_path = tid1_path.Append("comm");

  brillo::WriteStringToFile(tid1_comm_path, "123\n");
  std::string io_content1 =
      "rchar: 1131213\nwchar: 7819\nsyscr: 1787\nsyscw: 389\nread_bytes: "
      "114688\nwrite_bytes: 56\ncancelled_write_bytes: 0\n";
  brillo::WriteStringToFile(tid1_io_path, io_content1);

  // Turn off access to the process's 'root' directory.
  ASSERT_TRUE(base::SetPosixFilePermissions(pid1_path, 0000));

  std::vector<debugd::thread_io_stats> stats;
  debugd::LoadThreadIoStats(GetTestRootDir(), stats, 10);

  EXPECT_EQ(stats.size(), 0);
}

TEST_F(TopIoThreadsUtilsTest, TestSingleProcessWithInaccessibleTaskDirectory) {
  // Process #1 that includes a single thread
  base::FilePath pid1_path = GetTestRootDir().Append("1");
  base::FilePath pid1_task_path = pid1_path.Append("task");

  base::FilePath tid1_path = pid1_task_path.Append("1");
  base::FilePath tid1_io_path = tid1_path.Append("io");
  base::FilePath tid1_comm_path = tid1_path.Append("comm");

  brillo::WriteStringToFile(tid1_comm_path, "123\n");
  std::string io_content1 =
      "rchar: 1131213\nwchar: 7819\nsyscr: 1787\nsyscw: 389\nread_bytes: "
      "114688\nwrite_bytes: 56\ncancelled_write_bytes: 0\n";
  brillo::WriteStringToFile(tid1_io_path, io_content1);

  // Turn off access to the process's task directory.
  ASSERT_TRUE(base::SetPosixFilePermissions(pid1_task_path, 0000));

  std::vector<debugd::thread_io_stats> stats;
  debugd::LoadThreadIoStats(GetTestRootDir(), stats, 10);

  EXPECT_EQ(stats.size(), 0);
}

TEST_F(TopIoThreadsUtilsTest, TestSingleThreadWithNoCommDirectory) {
  // Process #1 that includes a single thread
  base::FilePath pid1_path = GetTestRootDir().Append("1");
  base::FilePath pid1_task_path = pid1_path.Append("task");

  // Thread #1 in process #1, without a 'comm' directory.
  base::FilePath tid1_path = pid1_task_path.Append("1");
  base::FilePath tid1_io_path = tid1_path.Append("io");

  std::string io_content1 =
      "rchar: 1131213\nwchar: 7819\nsyscr: 1787\nsyscw: 389\nread_bytes: "
      "114688\nwrite_bytes: 56\ncancelled_write_bytes: 0\n";
  brillo::WriteStringToFile(tid1_io_path, io_content1);

  std::vector<debugd::thread_io_stats> stats;
  debugd::LoadThreadIoStats(GetTestRootDir(), stats, 10);

  EXPECT_EQ(stats.size(), 0);
}

TEST_F(TopIoThreadsUtilsTest, TestSingleThreadWithNoIoDirectory) {
  // Process #1 that includes a single thread
  base::FilePath pid1_path = GetTestRootDir().Append("1");
  base::FilePath pid1_task_path = pid1_path.Append("task");

  // Thread #1 in process #1, without a 'io' directory.
  base::FilePath tid1_path = pid1_task_path.Append("1");
  base::FilePath tid1_comm_path = tid1_path.Append("comm");

  brillo::WriteStringToFile(tid1_comm_path, "123\n");

  std::vector<debugd::thread_io_stats> stats;
  debugd::LoadThreadIoStats(GetTestRootDir(), stats, 10);

  EXPECT_EQ(stats.size(), 0);
}

TEST_F(TopIoThreadsUtilsTest, TestTwoThreadsOneWithNoCommDirectory) {
  // Process #1 that includes two threads.
  base::FilePath pid1_path = GetTestRootDir().Append("1");
  base::FilePath pid1_task_path = pid1_path.Append("task");

  // Thread #1 in process #1.
  base::FilePath tid1_path = pid1_task_path.Append("1");
  base::FilePath tid1_comm_path = tid1_path.Append("comm");
  base::FilePath tid1_io_path = tid1_path.Append("io");

  brillo::WriteStringToFile(tid1_comm_path, "123\n");
  std::string io_content1 =
      "rchar: 1131213\nwchar: 7819\nsyscr: 1787\nsyscw: 389\nread_bytes: "
      "114688\nwrite_bytes: 56\ncancelled_write_bytes: 0\n";
  brillo::WriteStringToFile(tid1_io_path, io_content1);

  // Thread #2 in process #1, with no 'comm' directory.
  base::FilePath tid2_path = pid1_task_path.Append("2");
  base::FilePath tid2_io_path = tid2_path.Append("io");

  std::string io_content2 =
      "rchar: 1131301\nwchar: 8063\nsyscr: 1841\nsyscw: 396\nread_bytes: "
      "224688\nwrite_bytes: 66\ncancelled_write_bytes: 0\n";
  brillo::WriteStringToFile(tid2_io_path, io_content2);

  std::vector<debugd::thread_io_stats> stats;
  debugd::LoadThreadIoStats(GetTestRootDir(), stats, 10);

  EXPECT_EQ(stats.size(), 1);
}

TEST_F(TopIoThreadsUtilsTest, TestTwoThreadsOneWithIncompleteIoFile) {
  // Process #1 that includes two threads.
  base::FilePath pid1_path = GetTestRootDir().Append("1");
  base::FilePath pid1_task_path = pid1_path.Append("task");

  // Thread #1 in process #1.
  base::FilePath tid1_path = pid1_task_path.Append("1");
  base::FilePath tid1_comm_path = tid1_path.Append("comm");
  base::FilePath tid1_io_path = tid1_path.Append("io");

  brillo::WriteStringToFile(tid1_comm_path, "123\n");
  std::string io_content1 =
      "rchar: 1131213\nwchar: 7819\nsyscr: 1787\nsyscw: 389\nread_bytes: "
      "114688\nwrite_bytes: 56\ncancelled_write_bytes: 0\n";
  brillo::WriteStringToFile(tid1_io_path, io_content1);

  // Thread #2 in process #1, with an incomplete 'io' file
  base::FilePath tid2_path = pid1_task_path.Append("2");
  base::FilePath tid2_comm_path = tid2_path.Append("comm");
  base::FilePath tid2_io_path = tid2_path.Append("io");

  brillo::WriteStringToFile(tid2_comm_path, "456\n");
  // io file has only 4 fields; 2 are missing
  std::string io_content2 =
      "rchar: 1131301\nwchar: 8063\nsyscr: 1841\nsyscw: 396\nread_bytes: "
      "224688\n";
  brillo::WriteStringToFile(tid2_io_path, io_content2);

  std::vector<debugd::thread_io_stats> stats;
  debugd::LoadThreadIoStats(GetTestRootDir(), stats, 10);

  EXPECT_EQ(stats.size(), 1);

  EXPECT_EQ(stats[0].pid, 1);
  EXPECT_EQ(stats[0].tid, 1);
  EXPECT_EQ(stats[0].command, "123");
  EXPECT_EQ(stats[0].bytes_read, 114688);
  EXPECT_EQ(stats[0].bytes_written, 56);
}

TEST_F(TopIoThreadsUtilsTest, TestSingleThreadWithInaccessibleThreadDirectory) {
  // Process #1 that includes a single thread
  base::FilePath pid1_path = GetTestRootDir().Append("1");
  base::FilePath pid1_task_path = pid1_path.Append("task");

  base::FilePath tid1_path = pid1_task_path.Append("1");
  base::FilePath tid1_io_path = tid1_path.Append("io");
  base::FilePath tid1_comm_path = tid1_path.Append("comm");

  brillo::WriteStringToFile(tid1_comm_path, "123\n");
  std::string io_content1 =
      "rchar: 1131213\nwchar: 7819\nsyscr: 1787\nsyscw: 389\nread_bytes: "
      "114688\nwrite_bytes: 56\ncancelled_write_bytes: 0\n";
  brillo::WriteStringToFile(tid1_io_path, io_content1);

  // Turn off access to the thread's 'root' directory.
  ASSERT_TRUE(base::SetPosixFilePermissions(tid1_path, 0000));

  std::vector<debugd::thread_io_stats> stats;
  debugd::LoadThreadIoStats(GetTestRootDir(), stats, 10);

  EXPECT_EQ(stats.size(), 0);
}

TEST_F(TopIoThreadsUtilsTest, TestSingleProcessWithNonIntegerId) {
  // Process #1 that includes a single thread; however, the process
  // ID is non-integer.
  base::FilePath pid1_path = GetTestRootDir().Append("abc");
  base::FilePath pid1_task_path = pid1_path.Append("task");

  // Thread ID is non-integer.
  base::FilePath tid1_path = pid1_task_path.Append("1");
  base::FilePath tid1_io_path = tid1_path.Append("io");
  base::FilePath tid1_comm_path = tid1_path.Append("comm");

  brillo::WriteStringToFile(tid1_comm_path, "123\n");
  std::string io_content1 =
      "rchar: 1131213\nwchar: 7819\nsyscr: 1787\nsyscw: 389\nread_bytes: "
      "114688\nwrite_bytes: 56\ncancelled_write_bytes: 0\n";
  brillo::WriteStringToFile(tid1_io_path, io_content1);

  std::vector<debugd::thread_io_stats> stats;
  debugd::LoadThreadIoStats(GetTestRootDir(), stats, 10);

  EXPECT_EQ(stats.size(), 0);
}

TEST_F(TopIoThreadsUtilsTest, TestSingleThreadWithNonIntegerId) {
  // Process #1 that includes a single thread
  base::FilePath pid1_path = GetTestRootDir().Append("1");
  base::FilePath pid1_task_path = pid1_path.Append("task");

  // Thread ID is non-integer.
  base::FilePath tid1_path = pid1_task_path.Append("abc");
  base::FilePath tid1_io_path = tid1_path.Append("io");
  base::FilePath tid1_comm_path = tid1_path.Append("comm");

  brillo::WriteStringToFile(tid1_comm_path, "123\n");
  std::string io_content1 =
      "rchar: 1131213\nwchar: 7819\nsyscr: 1787\nsyscw: 389\nread_bytes: "
      "114688\nwrite_bytes: 56\ncancelled_write_bytes: 0\n";
  brillo::WriteStringToFile(tid1_io_path, io_content1);

  std::vector<debugd::thread_io_stats> stats;
  debugd::LoadThreadIoStats(GetTestRootDir(), stats, 10);

  EXPECT_EQ(stats.size(), 0);
}

TEST_F(TopIoThreadsUtilsTest, TestPrintIoStats) {
  std::vector<debugd::thread_io_stats> stats = {
      {1, 11, 123, 456, "command1"}, {2, 21, 789, 101112, "command2"}};
  std::ostringstream output_stream;

  debugd::PrintThreadIoStats(stats, output_stream);

  std::istringstream input_stream(output_stream.str());
  std::string tid_str, pid_str, bytes_read_str, bytes_written_str, command_str;
  input_stream >> std::ws >> tid_str >> pid_str >> bytes_read_str >>
      bytes_written_str >> command_str;
  EXPECT_EQ(tid_str, "TID");
  EXPECT_EQ(pid_str, "PID");
  EXPECT_EQ(bytes_read_str, "BYTES_READ");
  EXPECT_EQ(bytes_written_str, "BYTES_WRITTEN");
  EXPECT_EQ(command_str, "COMMAND");

  pid_t pid, tid;
  uint64_t bytes_read, bytes_written;
  std::string command;

  input_stream >> pid >> tid >> bytes_read >> bytes_written >> command >>
      std::ws;
  EXPECT_EQ(pid, 2);
  EXPECT_EQ(tid, 21);
  EXPECT_EQ(bytes_read, 789);
  EXPECT_EQ(bytes_written, 101112);
  EXPECT_EQ(command, "command2");

  input_stream >> pid >> tid >> bytes_read >> bytes_written >> command >>
      std::ws;
  EXPECT_EQ(pid, 1);
  EXPECT_EQ(tid, 11);
  EXPECT_EQ(bytes_read, 123);
  EXPECT_EQ(bytes_written, 456);
  EXPECT_EQ(command, "command1");

  std::string one_more_line;
  ASSERT_FALSE(std::getline(input_stream, one_more_line));
}

TEST_F(TopIoThreadsUtilsTest, TestPrintIoStatsWithEmptyList) {
  std::ostringstream output_stream;

  std::vector<debugd::thread_io_stats> stats;

  debugd::PrintThreadIoStats(stats, output_stream);

  std::istringstream input_stream(output_stream.str());

  std::string tid_str, pid_str, bytes_read_str, bytes_written_str, command_str;
  input_stream >> std::ws >> tid_str >> pid_str >> bytes_read_str >>
      bytes_written_str >> command_str >> std::ws;
  EXPECT_EQ(tid_str, "TID");
  EXPECT_EQ(pid_str, "PID");
  EXPECT_EQ(bytes_read_str, "BYTES_READ");
  EXPECT_EQ(bytes_written_str, "BYTES_WRITTEN");
  EXPECT_EQ(command_str, "COMMAND");

  std::string one_more_line;
  ASSERT_FALSE(std::getline(input_stream, one_more_line));
}
