// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <unistd.h>

#include <base/files/scoped_file.h>
#include <base/strings/stringprintf.h>
#include <gtest/gtest.h>

#include "debugd/src/sandboxed_process.h"

namespace debugd {

TEST(SandboxedProcessTest, ClosesUnusedFileDescriptors) {
  int fds[2];
  ASSERT_EQ(pipe(fds), 0);
  base::ScopedFD read_fd(fds[0]);
  base::ScopedFD write_fd(fds[1]);

  SandboxedProcess process;
  process.DisableSandbox();
  process.AddArg("/usr/bin/stat");
  process.AddArg(base::StringPrintf("/proc/self/fd/%d", read_fd.get()));
  process.AddArg(base::StringPrintf("/proc/self/fd/%d", write_fd.get()));

  // If unused file descriptors are properly closed before exec, stat will
  // fail with a non-zero exit code because /proc/self/fd/<fd> will not exist
  // in the child.
  // When close_unused_file_descriptors is false, child processes inherit the
  // parent's open file descriptors, causing stat to succeed (exit code 0).
  EXPECT_NE(process.Run(), 0);
}

TEST(SandboxedProcessTest, PreservesBoundFileDescriptors) {
  int fds[2];
  ASSERT_EQ(pipe(fds), 0);
  base::ScopedFD read_fd(fds[0]);
  base::ScopedFD write_fd(fds[1]);

  SandboxedProcess process;
  process.DisableSandbox();
  process.BindFd(read_fd.get(), read_fd.get());
  process.AddArg("/usr/bin/stat");
  process.AddArg(base::StringPrintf("/proc/self/fd/%d", read_fd.get()));

  // read_fd is explicitly bound with BindFd(fd, fd), so /proc/self/fd/<read_fd>
  // must remain open across exec in the child process, and stat should
  // return 0.
  EXPECT_EQ(process.Run(), 0);
}

}  // namespace debugd
