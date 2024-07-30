// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NETWORK_MOCK_DHCP_CLIENT_PROXY_H_
#define SHILL_NETWORK_MOCK_DHCP_CLIENT_PROXY_H_

#include <memory>
#include <string_view>

#include <base/functional/callback_helpers.h>
#include <gmock/gmock.h>

#include "shill/network/dhcp_client_proxy.h"

namespace shill {

class MockDHCPClientProxy : public DHCPClientProxy {
 public:
  MockDHCPClientProxy(std::string_view interface,
                      EventHandler* handler,
                      base::ScopedClosureRunner destroy_cb = {});
  ~MockDHCPClientProxy() override;

  MOCK_METHOD(bool, IsReady, (), (const, override));
  MOCK_METHOD(bool, Rebind, (), (override));
  MOCK_METHOD(bool, Release, (), (override));

 private:
  base::ScopedClosureRunner destroy_cb_;
};

class MockDHCPClientProxyFactory : public DHCPClientProxyFactory {
 public:
  MockDHCPClientProxyFactory();
  ~MockDHCPClientProxyFactory() override;

  MOCK_METHOD(std::unique_ptr<DHCPClientProxy>,
              Create,
              (std::string_view,
               Technology,
               const DHCPClientProxy::Options&,
               DHCPClientProxy::EventHandler*,
               net_base::IPFamily),
              (override));
};

}  // namespace shill
#endif  // SHILL_NETWORK_MOCK_DHCP_CLIENT_PROXY_H_
