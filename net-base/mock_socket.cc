// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net-base/mock_socket.h"

#include <fcntl.h>

#include <utility>

#include <base/files/scoped_file.h>
#include <base/logging.h>

namespace net_base {
namespace {

base::ScopedFD CreateFakeSocketFd() {
  // Create a real file descriptor for MockSocket.
  int sv[2];
  if (socketpair(AF_UNIX, SOCK_RAW, 0, sv) != 0) {
    PLOG(ERROR) << "Failed to create socket pair";
    return {};
  }

  close(sv[1]);
  return base::ScopedFD(sv[0]);
}

}  // namespace

MockSocket::MockSocket() : Socket(CreateFakeSocketFd()) {}
MockSocket::MockSocket(base::ScopedFD fd) : Socket(std::move(fd)) {}
MockSocket::~MockSocket() = default;

MockSocketFactory::MockSocketFactory() = default;
MockSocketFactory::~MockSocketFactory() = default;

}  // namespace net_base
