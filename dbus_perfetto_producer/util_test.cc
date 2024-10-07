// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dbus_perfetto_producer/util.cc"

#include <base/check.h>
#include <gtest/gtest.h>

namespace dbus_perfetto_producer {

namespace {

class DBusMonitorTest : public ::testing::Test {
 public:
  DBusMonitorTest() = default;
  DBusMonitorTest(const DBusMonitorTest&) = delete;
  DBusMonitorTest& operator=(const DBusMonitorTest&) = delete;
};

void WriteReadIntTest(int fd[2], uint64_t input) {
  uint64_t output;
  ASSERT_TRUE(WriteInt(fd[1], input));
  ASSERT_TRUE(ReadInt(fd[0], output));
  ASSERT_EQ(input, output);
}

void WriteReadBufTest(int fd[2], const char* input) {
  std::string output;
  ASSERT_TRUE(WriteBuf(fd[1], input));
  ASSERT_TRUE(ReadBuf(fd[0], output));
  if (input) {
    ASSERT_EQ(input, output);
  } else {
    ASSERT_TRUE(output.empty());
  }
}

}  // namespace

TEST_F(DBusMonitorTest, WriteReadIntTests) {
  int fd[2];
  ASSERT_EQ(pipe(fd), 0);
  WriteReadIntTest(fd, 0);
  WriteReadIntTest(fd, 1);
  WriteReadIntTest(fd, 12345);
  WriteReadIntTest(fd, 10000000000000);
  close(fd[0]);
  close(fd[1]);
}

TEST_F(DBusMonitorTest, WriteReadBufTests) {
  int fd[2];
  ASSERT_EQ(pipe(fd), 0);
  WriteReadBufTest(fd, nullptr);
  WriteReadBufTest(fd, "");
  WriteReadBufTest(fd, "a");
  WriteReadBufTest(fd, "Random Buffer 12345");
  close(fd[0]);
  close(fd[1]);
}

}  // namespace dbus_perfetto_producer
