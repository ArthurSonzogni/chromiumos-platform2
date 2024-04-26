// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_DOWNSTREAM_NETWORK_SERVICE_H_
#define PATCHPANEL_DOWNSTREAM_NETWORK_SERVICE_H_

#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <base/files/scoped_file.h>
#include <base/functional/callback_helpers.h>
#include <base/memory/weak_ptr.h>
#include <patchpanel/proto_bindings/patchpanel_service.pb.h>

#include "patchpanel/counters_service.h"
#include "patchpanel/datapath.h"
#include "patchpanel/dhcp_server_controller.h"
#include "patchpanel/downstream_network_info.h"
#include "patchpanel/forwarding_service.h"
#include "patchpanel/guest_ipv6_service.h"
#include "patchpanel/lifeline_fd_service.h"
#include "patchpanel/metrics.h"
#include "patchpanel/rtnl_client.h"
#include "patchpanel/shill_client.h"
#include "patchpanel/system.h"

namespace patchpanel {

// Services used to manage TetheredNetworks and LocalOnlyNetworks created by
// patchpanel for a DBus client.
class DownstreamNetworkService {
 public:
  DownstreamNetworkService(MetricsLibraryInterface* metrics,
                           System* system,
                           Datapath* datapath,
                           RoutingService* routing_svc,
                           ForwardingService* forwarding_svc,
                           RTNLClient* rtnl_client,
                           LifelineFDService* lifeline_fd_svc,
                           ShillClient* shill_client,
                           GuestIPv6Service* ipv6_svc,
                           CountersService* counters_svc);

  DownstreamNetworkService(const DownstreamNetworkService&) = delete;
  DownstreamNetworkService& operator=(const DownstreamNetworkService&) = delete;
  virtual ~DownstreamNetworkService();

  // Creates an L3 network on a network interface and tethered to an upstream
  // network. Returns the result of the operation as a TetheredNetworkResponse
  // protobuf message.
  patchpanel::TetheredNetworkResponse CreateTetheredNetwork(
      const patchpanel::TetheredNetworkRequest& request,
      base::ScopedFD client_fd);

  // Creates a local-only L3 network on a network interface. Returns the result
  // of the operation as a TetheredNetworkResponse protobuf message.
  patchpanel::LocalOnlyNetworkResponse CreateLocalOnlyNetwork(
      const patchpanel::LocalOnlyNetworkRequest& request,
      base::ScopedFD client_fd);

  // Provides L3 and DHCP client information about clients connected to a
  // network created with CreateTetheredNetwork or CreateLocalOnlyNetwork.
  patchpanel::GetDownstreamNetworkInfoResponse GetDownstreamNetworkInfo(
      std::string_view downstream_ifname) const;

  void UpdateDeviceIPConfig(const ShillClient::Device& shill_device);

  // Returns the CurHopLimit of upstream from sysctl minus 1.
  static std::optional<int> CalculateDownstreamCurHopLimit(
      System* system, std::string_view upstream_iface);

 private:
  // Creates a downstream L3 network on the network interface specified by the
  // |info|. If successful, |client_fd| is monitored and triggers the teardown
  // of the network setup when closed.
  std::pair<DownstreamNetworkResult, std::unique_ptr<DownstreamNetwork>>
  HandleDownstreamNetworkInfo(base::ScopedFD client_fd,
                              std::unique_ptr<DownstreamNetworkInfo> info);
  void OnDownstreamNetworkAutoclose(std::string_view downstream_ifname);
  std::vector<DownstreamClientInfo> GetDownstreamClientInfo(
      std::string_view downstream_ifname) const;
  void Stop();

  // b/294287313: Temporary solution to support tethering with a multiplexed PDN
  // brought up specifically for tethering and with no associated shill Device.
  // This method creates a fake ShillClient::Device and creates the minimal
  // Datapath setup to support DownstreamNetworkService::CreateTetheredNetwork()
  // pointing at |upstream_ifname| as the upstream network.
  std::optional<ShillClient::Device> StartTetheringUpstreamNetwork(
      const TetheredNetworkRequest& request);
  // Tears down the minimal Datapath setup created with
  // StartTetheringUpstreamNetwork().
  void StopTetheringUpstreamNetwork(
      // const std::string& upstream_ifname);
      const ShillClient::Device& upstream_network);

  // Ownned by PatchpanelDaemon
  MetricsLibraryInterface* metrics_;
  System* system_;

  // Ownned by Manager
  Datapath* datapath_;
  RoutingService* routing_svc_;
  ForwardingService* forwarding_svc_;
  RTNLClient* rtnl_client_;
  LifelineFDService* lifeline_fd_svc_;
  ShillClient* shill_client_;
  GuestIPv6Service* ipv6_svc_;
  CountersService* counters_svc_;

  // All external network interfaces currently managed by patchpanel through
  // the CreateTetheredNetwork or CreateLocalOnlyNetwork DBus APIs, keyed by
  // their downstream interface name.
  std::map<std::string, std::unique_ptr<DownstreamNetworkInfo>, std::less<>>
      downstream_networks_;
  // The DHCP server controllers, keyed by its downstream interface.
  std::map<std::string, std::unique_ptr<DHCPServerController>>
      dhcp_server_controllers_;

  base::WeakPtrFactory<DownstreamNetworkService> weak_factory_{this};
};

}  // namespace patchpanel

#endif  // PATCHPANEL_DOWNSTREAM_NETWORK_SERVICE_H_
