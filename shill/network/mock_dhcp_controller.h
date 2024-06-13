// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NETWORK_MOCK_DHCP_CONTROLLER_H_
#define SHILL_NETWORK_MOCK_DHCP_CONTROLLER_H_

#include <memory>
#include <string_view>

#include <chromeos/net-base/network_config.h>
#include <gmock/gmock.h>

#include "shill/network/dhcp_client_proxy.h"
#include "shill/network/dhcp_controller.h"
#include "shill/network/dhcpv4_config.h"
#include "shill/technology.h"
#include "shill/time.h"

namespace shill {

class MockDHCPController : public DHCPController {
 public:
  MockDHCPController(EventDispatcher* dispatcher,
                     Metrics* metrics,
                     Time* time,
                     DHCPClientProxyFactory* dhcp_client_proxy_factory,
                     std::string_view device_name,
                     Technology technology,
                     const DHCPClientProxy::Options& options,
                     UpdateCallback update_callback,
                     DropCallback drop_callback);

  ~MockDHCPController() override;

  void TriggerUpdateCallback(const net_base::NetworkConfig& network_config,
                             const DHCPv4Config::Data& dhcp_data);
  void TriggerDropCallback(bool is_voluntary);

  MOCK_METHOD(bool, ReleaseIP, (ReleaseReason), (override));
  MOCK_METHOD(bool, RenewIP, (), (override));

 private:
  UpdateCallback update_callback_;
  DropCallback drop_callback_;
};

class MockDHCPControllerFactory : public DHCPControllerFactory {
 public:
  MockDHCPControllerFactory();
  ~MockDHCPControllerFactory() override;

  MOCK_METHOD(std::unique_ptr<DHCPController>,
              Create,
              (std::string_view device_name,
               Technology technology,
               const DHCPClientProxy::Options& options,
               DHCPController::UpdateCallback update_callback,
               DHCPController::DropCallback drop_callback),
              (override));
};

}  // namespace shill
#endif  // SHILL_NETWORK_MOCK_DHCP_CONTROLLER_H_
