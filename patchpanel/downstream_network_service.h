// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_DOWNSTREAM_NETWORK_SERVICE_H_
#define PATCHPANEL_DOWNSTREAM_NETWORK_SERVICE_H_

#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include <net-base/ipv4_address.h>
#include <net-base/ipv6_address.h>
#include <patchpanel/proto_bindings/patchpanel_service.pb.h>

#include "patchpanel/dhcp_server_controller.h"
#include "patchpanel/mac_address_generator.h"
#include "patchpanel/metrics.h"
#include "patchpanel/shill_client.h"

namespace patchpanel {

// Describes the type of CreateNetwork request issued by shill.
enum class DownstreamNetworkTopology {
  // CreateTetheredNetwork DBus method call.
  kTethering,
  // CreateLocalOnlyNetwork DBus method call.
  kLocalOnly,
};

// Describes a CreateNetwork request issued by shill.
struct DownstreamNetworkInfo {
  DownstreamNetworkTopology topology;
  // The upstream interface is only defined for Tethering. It is left undefined
  // for LocalOnlyNetwork.
  std::optional<ShillClient::Device> upstream_device;
  std::string downstream_ifname;
  // IPv4 CIDR of the DUT on the downstream network. This is the effective
  // gateway address for clients connected on the network.
  net_base::IPv4CIDR ipv4_cidr;
  // The MTU of the upstream. std::nullopt means the MTU is set to the default
  // value (i.e. 1500).
  std::optional<int> mtu;

  // Set to true if IPv4 DHCP server is created at the downstream.
  bool enable_ipv4_dhcp;
  // IPv4 DHCP IP range, only used when |enable_ipv4_dhcp| is true.
  net_base::IPv4Address ipv4_dhcp_start_addr;
  net_base::IPv4Address ipv4_dhcp_end_addr;
  //  The DNS server of the DHCP option, only used when |enable_ipv4_dhcp| is
  //  true.
  std::vector<net_base::IPv4Address> dhcp_dns_servers;
  // The domain search of the DHCP option, only used when |enable_ipv4_dhcp| is
  // true.
  std::vector<std::string> dhcp_domain_searches;
  // The extra DHCP options, only used when |enable_ipv4_dhcp| is true.
  DHCPServerController::Config::DHCPOptions dhcp_options;

  // Set to true if GuestIPv6Service is used on the downstream network.
  bool enable_ipv6;
  // TODO(b/239559602) Add IPv6 configuration for LocalOnlyNetwork.

  // Creates the DownstreamNetworkInfo instance from TetheredNetworkRequest.
  static std::optional<DownstreamNetworkInfo> Create(
      const TetheredNetworkRequest& request,
      const ShillClient::Device& shill_device);
  // Creates the DownstreamNetworkInfo instance from LocalOnlyNetworkRequest.
  static std::optional<DownstreamNetworkInfo> Create(
      const LocalOnlyNetworkRequest& request);

  // Creates the configuration of the DHCPServerController.
  std::optional<DHCPServerController::Config> ToDHCPServerConfig() const;
};

// Describes a downstream client's information. See NetworkClientInfo in
// patchpanel_service.proto.
struct DownstreamClientInfo {
  MacAddress mac_addr;
  net_base::IPv4Address ipv4_addr;
  std::vector<net_base::IPv6Address> ipv6_addresses;
  std::string hostname;
  std::string vendor_class;
};

CreateDownstreamNetworkResult DownstreamNetworkResultToUMAEvent(
    patchpanel::DownstreamNetworkResult result);

std::ostream& operator<<(std::ostream& stream,
                         const DownstreamNetworkInfo& info);

}  // namespace patchpanel

#endif  // PATCHPANEL_DOWNSTREAM_NETWORK_SERVICE_H_
