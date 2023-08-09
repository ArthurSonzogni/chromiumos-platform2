// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net-base/mock_socket.h"

#include <fcntl.h>

#include <utility>

namespace net_base {

MockSocket::MockSocket()
    : Socket(base::ScopedFD(open("/dev/null", O_RDONLY))) {}
MockSocket::MockSocket(base::ScopedFD fd) : Socket(std::move(fd)) {}
MockSocket::~MockSocket() = default;

}  // namespace net_base
