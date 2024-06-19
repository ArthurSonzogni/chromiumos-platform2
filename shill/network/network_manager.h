// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NETWORK_NETWORK_MANAGER_H_
#define SHILL_NETWORK_NETWORK_MANAGER_H_

#include <map>
#include <memory>
#include <string_view>

#include "shill/network/network.h"

namespace shill {

class ControlInterface;
class EventDispatcher;
class Metrics;

// This class is responsible for creating and tracking all the Network
// instances. Also, it could configure the Network instances globally.
class NetworkManager : Network::EventHandler {
 public:
  NetworkManager(ControlInterface* control_interface,
                 EventDispatcher* dispatcher,
                 Metrics* metrics);
  ~NetworkManager() override;

  // Creates a Network instance, and tracks the instance at |alive_networks_|.
  // TODO(b/273743901): Make the method asynchronous after creating Network
  // objects at patchpanel.
  std::unique_ptr<Network> CreateNetwork(int interface_index,
                                         std::string_view interface_name,
                                         Technology technology,
                                         bool fixed_ip_params,
                                         patchpanel::Client* patchpanel_client);

  // Gets the Network instance querying by |network_id|. Returns nullptr if no
  // Network is found.
  // Note: there is no guarantee about the lifetime of the returned Network. The
  // caller should not save the pointer and use it later.
  virtual Network* GetNetwork(int network_id) const;

  // Enables or disables the CAPPORT functionality to all the Network instances,
  // including the instances created later.
  void SetCapportEnabled(bool enabled);

 private:
  // Implements Network::EventHandler.
  void OnNetworkDestroyed(int network_id, int interface_index) override;

  ControlInterface* const control_interface_;
  EventDispatcher* const dispatcher_;
  Metrics* const metrics_;

  std::unique_ptr<DHCPClientProxyFactory> dhcp_client_proxy_factory_;

  // Tracks all the alive Network instances.
  std::map<int /* network_id */, Network*> alive_networks_;

  bool capport_enabled_ = true;
};

}  // namespace shill
#endif  // SHILL_NETWORK_NETWORK_MANAGER_H_
