// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/network_manager.h"

#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <base/logging.h>
#include <base/memory/ptr_util.h>

#include "shill/control_interface.h"

namespace shill {

NetworkManager::NetworkManager(ControlInterface* control_interface,
                               EventDispatcher* dispatcher,
                               Metrics* metrics)
    : control_interface_(control_interface),
      dispatcher_(dispatcher),
      metrics_(metrics),
      legacy_dhcpcd_proxy_factory_(
          control_interface_->CreateDHCPClientProxyFactory()),
      dhcpcd_proxy_factory_(std::make_unique<DHCPCDProxyFactory>()) {}
NetworkManager::~NetworkManager() = default;

std::unique_ptr<Network> NetworkManager::CreateNetwork(
    int interface_index,
    std::string_view interface_name,
    Technology technology,
    bool fixed_ip_params,
    patchpanel::Client* patchpanel_client) {
  auto network = base::WrapUnique(new Network(
      interface_index, std::string(interface_name), technology, fixed_ip_params,
      control_interface_, dispatcher_, metrics_, patchpanel_client,
      std::make_unique<DHCPControllerFactory>(
          dispatcher_, metrics_, Time::GetInstance(),
          legacy_dhcpcd_proxy_factory_.get()),
      std::make_unique<DHCPControllerFactory>(dispatcher_, metrics_,
                                              Time::GetInstance(),
                                              dhcpcd_proxy_factory_.get())));
  network->SetCapportEnabled(capport_enabled_);
  network->RegisterEventHandler(this);
  alive_networks_.insert(std::make_pair(network->network_id(), network.get()));
  return network;
}

Network* NetworkManager::GetNetwork(int network_id) const {
  const auto iter = alive_networks_.find(network_id);
  if (iter != alive_networks_.end()) {
    return iter->second;
  }
  return nullptr;
}

void NetworkManager::SetCapportEnabled(bool enabled) {
  if (capport_enabled_ == enabled) {
    return;
  }

  LOG(INFO) << __func__ << ": Set to " << std::boolalpha << enabled;
  capport_enabled_ = enabled;
  for (const auto& [id, network] : alive_networks_) {
    network->SetCapportEnabled(capport_enabled_);
  }
}

void NetworkManager::NotifyDHCPEvent(
    const std::map<std::string, std::string>& configuration) {
  dhcpcd_proxy_factory_->OnDHCPEvent(configuration);
}

void NetworkManager::OnNetworkDestroyed(int network_id, int interface_index) {
  if (alive_networks_.erase(network_id) == 0) {
    LOG(WARNING) << __func__ << ": erase the stale Network " << network_id;
  }
}
}  // namespace shill
