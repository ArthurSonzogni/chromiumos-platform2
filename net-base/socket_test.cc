// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net-base/socket.h"

#include <fcntl.h>

#include <memory>
#include <utility>

#include <base/files/scoped_file.h>
#include <gtest/gtest.h>

namespace net_base {
namespace {

TEST(Socket, CreateFromFd) {
  base::ScopedFD fd(open("/dev/null", O_RDONLY));
  int raw_fd = fd.get();

  auto socket = Socket::CreateFromFd(std::move(fd));
  EXPECT_EQ(socket->Get(), raw_fd);
}

TEST(Socket, CreateFromFd_Invalid) {
  auto socket = Socket::CreateFromFd(base::ScopedFD());
  EXPECT_EQ(socket, nullptr);
}

TEST(Socket, Release) {
  base::ScopedFD fd(open("/dev/null", O_RDONLY));
  int raw_fd = fd.get();

  // Socket::Release() returns the raw fd, and not close the fd.
  auto socket = Socket::CreateFromFd(std::move(fd));
  EXPECT_EQ(Socket::Release(std::move(socket)), raw_fd);
  EXPECT_EQ(close(raw_fd), 0);
}

}  // namespace
}  // namespace net_base
