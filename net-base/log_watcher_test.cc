// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net-base/log_watcher.h"

#include <memory>
#include <utility>

#include <base/logging.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/functional/bind.h>
#include <base/functional/callback_helpers.h>
#include <base/test/task_environment.h>
#include <gtest/gtest.h>
#include <gmock/gmock.h>

namespace net_base {
namespace {

class LogWatcherTest : public ::testing::Test {
 protected:
  void SetUp() override {
    int pipe_fds[2];
    ASSERT_EQ(0, pipe(pipe_fds));
    base::ScopedFD read_fd(pipe_fds[0]);
    write_fd_.reset(pipe_fds[1]);

    log_watcher_ = LogWatcher::Create(
        std::move(read_fd), base::BindRepeating(&LogWatcherTest::OnLogReady,
                                                base::Unretained(this)));
  }

  MOCK_METHOD(void, OnLogReady, (std::string_view));

  int write_fd() { return write_fd_.get(); }

  // The environment instances which are required for using
  // base::FileDescriptorWatcher::WatchReadable. Declared them first to ensure
  // they are the last things to be cleaned up.
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::MainThreadType::IO};

  base::ScopedFD write_fd_;
  std::unique_ptr<LogWatcher> log_watcher_;
};

TEST_F(LogWatcherTest, LogReadyCallback) {
  EXPECT_CALL(*this, OnLogReady("hello, world"));
  EXPECT_CALL(*this, OnLogReady("foo bar"));
  EXPECT_CALL(*this, OnLogReady("abc")).Times(0);

  // The log ends with the newline character.
  base::WriteFileDescriptor(write_fd(), "hello, world\n");

  // Combine the log until the newline character.
  base::WriteFileDescriptor(write_fd(), "foo ");
  base::WriteFileDescriptor(write_fd(), "bar\n");

  // The last log without newline character will be dropped.
  base::WriteFileDescriptor(write_fd(), "abc");
  base::RunLoop().RunUntilIdle();
}

}  // namespace
}  // namespace net_base
