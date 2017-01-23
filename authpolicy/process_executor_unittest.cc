// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>

#include <stdlib.h>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <authpolicy/process_executor.h>

namespace {

const char kCmdCat[] = "/bin/cat";
const char kCmdEcho[] = "/bin/echo";
const char kCmdFalse[] = "/bin/false";
const char kCmdGrep[] = "/bin/grep";
const char kCmdTee[] = "/usr/bin/tee";
const char kCmdPrintEnv[] = "/usr/bin/printenv";
const char kEnvVar[] = "PROCESS_EXECUTOR_TEST_ENV_VAR";
const char kEnvVar2[] = "PROCESS_EXECUTOR_TEST_2_ENV_VAR";
const char kGrepTestText[] = "This is a test.\n";
const char kGrepTestToken[] = "test";
const char kFileDoesNotExist[] = "does_not_exist_khsdgviu";
const char kLargeTestString[] = "I like recursion because ";

int GetPipeSize() {
  int fds[2] = {-1, -1};
  EXPECT_EQ(pipe(fds), 0);
  base::ScopedFD fd0(fds[0]);
  base::ScopedFD fd1(fds[1]);
  int pipe_size = fcntl(fd1.get(), F_GETPIPE_SZ);
  EXPECT_NE(pipe_size, -1);
  return pipe_size;
}

}  // namespace

namespace authpolicy {

class ProcessExecutorTest : public ::testing::Test {
 public:
  ProcessExecutorTest() {}
  ~ProcessExecutorTest() override {}

 private:
  DISALLOW_COPY_AND_ASSIGN(ProcessExecutorTest);
};

// Calling Execute() on an instance with no command args should succeed.
TEST_F(ProcessExecutorTest, EmptyArgs) {
  ProcessExecutor cmd({});
  EXPECT_TRUE(cmd.Execute());
  EXPECT_EQ(cmd.GetExitCode(), 0);
  EXPECT_TRUE(cmd.GetStdout().empty());
  EXPECT_TRUE(cmd.GetStderr().empty());
}

// Execute command with no additional args.
TEST_F(ProcessExecutorTest, CommandWithNoArgs) {
  ProcessExecutor cmd({kCmdEcho});
  EXPECT_TRUE(cmd.Execute());
  EXPECT_EQ(cmd.GetExitCode(), 0);
  EXPECT_FALSE(cmd.GetStdout().empty());
  EXPECT_TRUE(cmd.GetStderr().empty());
}

// Executing non-existing command should result in error in stderr.
TEST_F(ProcessExecutorTest, NonExistingCommand) {
  ProcessExecutor cmd({kCmdCat, kFileDoesNotExist});
  EXPECT_FALSE(cmd.Execute());
  EXPECT_NE(cmd.GetExitCode(), 0);
  EXPECT_EQ(cmd.GetStdout(), "");
  EXPECT_EQ(cmd.GetStderr(),
            base::StringPrintf("cat: %s: No such file or directory\n",
                               kFileDoesNotExist));
}

// Repeated execution should have no side effects on stdout.
TEST_F(ProcessExecutorTest, RepeatedExecutionWorks_Stdout) {
  ProcessExecutor cmd({kCmdPrintEnv, kEnvVar});
  cmd.SetEnv(kEnvVar, "first");
  EXPECT_TRUE(cmd.Execute());
  EXPECT_EQ(cmd.GetExitCode(), 0);
  EXPECT_EQ(cmd.GetStdout(), "first\n");
  EXPECT_TRUE(cmd.GetStderr().empty());

  cmd.SetEnv(kEnvVar, "second");
  EXPECT_TRUE(cmd.Execute());
  EXPECT_EQ(cmd.GetExitCode(), 0);
  EXPECT_EQ(cmd.GetStdout(), "second\n");
  EXPECT_TRUE(cmd.GetStderr().empty());
}  // namespace authpolicy

// Repeated execution should have no side effects on stderr.
TEST_F(ProcessExecutorTest, RepeatedExecutionWorks_Stderr) {
  ProcessExecutor cmd({kCmdCat, kFileDoesNotExist});
  EXPECT_FALSE(cmd.Execute());
  EXPECT_NE(cmd.GetExitCode(), 0);
  EXPECT_TRUE(cmd.GetStdout().empty());
  std::string stderr = cmd.GetStderr();  // Important: Make copy!
  EXPECT_FALSE(stderr.empty());

  EXPECT_FALSE(cmd.Execute());
  EXPECT_NE(cmd.GetExitCode(), 0);
  EXPECT_TRUE(cmd.GetStdout().empty());
  EXPECT_EQ(cmd.GetStderr(), stderr);
}

// Reading output from stdout.
TEST_F(ProcessExecutorTest, ReadFromStdout) {
  ProcessExecutor cmd({kCmdEcho, "test"});
  EXPECT_TRUE(cmd.Execute());
  EXPECT_EQ(cmd.GetExitCode(), 0);
  EXPECT_EQ(cmd.GetStdout(), "test\n");
  EXPECT_TRUE(cmd.GetStderr().empty());
}

// Reading output from stderr.
TEST_F(ProcessExecutorTest, ReadFromStderr) {
  ProcessExecutor cmd({kCmdGrep, "--invalid_arg"});
  EXPECT_FALSE(cmd.Execute());
  EXPECT_NE(cmd.GetExitCode(), 0);
  EXPECT_TRUE(cmd.GetStdout().empty());
  EXPECT_TRUE(base::StartsWith(cmd.GetStderr(), kCmdGrep,
                               base::CompareCase::SENSITIVE));
}

// Reading large amounts of output from stdout to test piping (triggers pipe
// block if done improperly).
TEST_F(ProcessExecutorTest, ReadLargeStringFromStdout) {
  // Target size should be much bigger than the pipe buffer size. In a test I
  // able to write more than 2x the pipe size to a blocking pipe, not sure why
  // this was possible. Usually, GetPipeSize() is around 64 kb.
  const int kTargetStringSize = GetPipeSize() * 16 + 1024;
  const int kNumRepeats = kTargetStringSize / strlen(kLargeTestString);
  std::string large_string;
  large_string.reserve(strlen(kLargeTestString) * kNumRepeats);
  for (int n = 0; n < kNumRepeats; ++n)
    large_string += kLargeTestString;
  ProcessExecutor cmd({kCmdTee, "/dev/stderr"});
  cmd.SetInputString(large_string);
  EXPECT_TRUE(cmd.Execute());
  EXPECT_EQ(cmd.GetExitCode(), 0);
  EXPECT_EQ(cmd.GetStdout(), large_string);
  EXPECT_EQ(cmd.GetStderr(), large_string);
}

// Getting exit codes.
TEST_F(ProcessExecutorTest, GetExitCode) {
  ProcessExecutor cmd({kCmdFalse});
  EXPECT_FALSE(cmd.Execute());
  EXPECT_EQ(cmd.GetExitCode(), 1);
}

// Setting input file.
TEST_F(ProcessExecutorTest, SetInputFile) {
  int input_pipes[2];
  EXPECT_TRUE(base::CreateLocalNonBlockingPipe(input_pipes));
  base::ScopedFD stdin_read_end(input_pipes[0]);
  base::ScopedFD stdin_write_end(input_pipes[1]);
  size_t num_chars = strlen(kGrepTestText);
  EXPECT_EQ(write(stdin_write_end.get(), kGrepTestText, num_chars), num_chars);
  stdin_write_end.reset();
  // Note: grep reads from stdin if no file arg is specified.
  ProcessExecutor cmd({kCmdGrep, kGrepTestToken});
  cmd.SetInputFile(stdin_read_end.get());
  EXPECT_TRUE(cmd.Execute());
  EXPECT_EQ(cmd.GetExitCode(), 0);
  EXPECT_EQ(cmd.GetStdout(), kGrepTestText);
  EXPECT_TRUE(cmd.GetStderr().empty());
}

// Setting an invalid input file results in an error code, but no error message.
TEST_F(ProcessExecutorTest, SetInvalidInputFile) {
  ProcessExecutor cmd({kCmdEcho, "test"});
  cmd.SetInputFile(-3);
  EXPECT_FALSE(cmd.Execute());
  EXPECT_EQ(cmd.GetExitCode(), 127);
  EXPECT_TRUE(cmd.GetStdout().empty());
  EXPECT_TRUE(cmd.GetStderr().empty());
}

// Setting an environment variable.
TEST_F(ProcessExecutorTest, SetEnvVariable) {
  ProcessExecutor cmd({kCmdPrintEnv, kEnvVar});
  cmd.SetEnv(kEnvVar, "test");
  EXPECT_TRUE(cmd.Execute());
  EXPECT_EQ(cmd.GetExitCode(), 0);
  EXPECT_EQ(cmd.GetStdout(), "test\n");
  EXPECT_TRUE(cmd.GetStderr().empty());
}

// The executor clears environment variables during execution, sets its own list
// and restores the old ones afterwards.
TEST_F(ProcessExecutorTest, ClearsEnvVariables) {
  setenv(kEnvVar, "1", 1);
  EXPECT_STREQ(getenv(kEnvVar), "1");
  ProcessExecutor cmd({kCmdPrintEnv});
  cmd.SetEnv(kEnvVar2, "2");
  EXPECT_TRUE(cmd.Execute());
  EXPECT_EQ(cmd.GetExitCode(), 0);
  EXPECT_EQ(cmd.GetStdout().find(kEnvVar), std::string::npos);
  EXPECT_NE(cmd.GetStdout().find(kEnvVar2), std::string::npos);
  EXPECT_TRUE(cmd.GetStderr().empty());
  EXPECT_STREQ(getenv(kEnvVar), "1");
  EXPECT_EQ(getenv(kEnvVar2), nullptr);
}

// Make sure you can't inject arbitrary commands in args
TEST_F(ProcessExecutorTest, NoSideEffects) {
  ProcessExecutor cmd({kCmdEcho, "test; ls"});
  EXPECT_TRUE(cmd.Execute());
  EXPECT_EQ(cmd.GetExitCode(), 0);
  EXPECT_EQ(cmd.GetStdout(), "test; ls\n");
  EXPECT_TRUE(cmd.GetStderr().empty());
}

// Commands must start with /
TEST_F(ProcessExecutorTest, CommandsMustUseAbsolutePaths) {
  ProcessExecutor cmd({"echo", "test"});
  EXPECT_FALSE(cmd.Execute());
}

}  // namespace authpolicy
