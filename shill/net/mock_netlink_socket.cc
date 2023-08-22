// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/net/mock_netlink_socket.h"

#include <memory>

#include <net-base/mock_socket.h>

namespace shill {

MockNetlinkSocket::MockNetlinkSocket()
    : NetlinkSocket(std::make_unique<net_base::MockSocket>()) {}
MockNetlinkSocket::~MockNetlinkSocket() = default;

}  // namespace shill
