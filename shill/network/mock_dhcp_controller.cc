// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/mock_dhcp_controller.h"

#include <string_view>
#include <utility>

#include <chromeos/net-base/network_config.h>

#include "shill/network/dhcp_client_proxy.h"
#include "shill/network/dhcp_controller.h"
#include "shill/network/dhcpv4_config.h"
#include "shill/technology.h"
#include "shill/time.h"

namespace shill {

MockDHCPController::MockDHCPController(
    EventDispatcher* dispatcher,
    Metrics* metrics,
    Time* time,
    DHCPClientProxyFactory* dhcp_client_proxy_factory,
    std::string_view device_name,
    Technology technology,
    const DHCPClientProxy::Options& options,
    UpdateCallback update_callback,
    DropCallback drop_callback)
    : DHCPController(dispatcher,
                     metrics,
                     time,
                     dhcp_client_proxy_factory,
                     device_name,
                     technology,
                     options,
                     base::DoNothing(),
                     base::DoNothing()),
      update_callback_(std::move(update_callback)),
      drop_callback_(std::move(drop_callback)) {}

MockDHCPController::~MockDHCPController() = default;

void MockDHCPController::TriggerUpdateCallback(
    const net_base::NetworkConfig& network_config,
    const DHCPv4Config::Data& dhcp_data) {
  update_callback_.Run(network_config, dhcp_data, /*new_lease_acquired=*/true);
}

void MockDHCPController::TriggerDropCallback(bool is_voluntary) {
  drop_callback_.Run(is_voluntary);
}

MockDHCPControllerFactory::MockDHCPControllerFactory()
    : DHCPControllerFactory(nullptr, nullptr, nullptr, nullptr) {}

MockDHCPControllerFactory::~MockDHCPControllerFactory() = default;

}  // namespace shill
