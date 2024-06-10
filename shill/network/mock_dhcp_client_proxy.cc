// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/mock_dhcp_client_proxy.h"

#include <string_view>
#include <utility>

namespace shill {

MockDHCPClientProxy::MockDHCPClientProxy(std::string_view interface,
                                         EventHandler* handler,
                                         base::ScopedClosureRunner destroy_cb)
    : DHCPClientProxy(interface, handler), destroy_cb_(std::move(destroy_cb)) {}

MockDHCPClientProxy::~MockDHCPClientProxy() = default;

MockDHCPClientProxyFactory::MockDHCPClientProxyFactory() = default;

MockDHCPClientProxyFactory::~MockDHCPClientProxyFactory() = default;

}  // namespace shill
