// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NETWORK_NETWORK_CONFIG_H_
#define SHILL_NETWORK_NETWORK_CONFIG_H_

#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include <net-base/ip_address.h>
#include <net-base/ipv4_address.h>
#include <net-base/ipv6_address.h>

namespace shill {

// Properties related to the IP layer used to represent a configuration.
// TODO(b/269401899): Add more fields and replace IPConfig::Properties.
// TODO(b/269401899): Add unit tests.
struct NetworkConfig {
  NetworkConfig();
  ~NetworkConfig();

  bool IsEmpty() const;

  // IPv4 configurations. If |ipv4_address| is null, no IPv4 is configured on
  // the Network. If |ipv4_address| is present but |ipv4_gateway| is null,
  // routes are to be added on-link to the netdevice.
  std::optional<net_base::IPv4CIDR> ipv4_address;
  std::optional<net_base::IPv4Address> ipv4_broadcast;
  std::optional<net_base::IPv4Address> ipv4_gateway;

  // IPv6 configurations. If |ipv6_gateway| is null, routes are to be added
  // on-link to the netdevice.
  std::vector<net_base::IPv6CIDR> ipv6_addresses;
  std::optional<net_base::IPv6Address> ipv6_gateway;

  // Routing configurations. If a destination is included, it will be routed
  // through the gateway of corresponding IP family (or on-link if gateway is
  // null). |ipv4_default_route| is a historical field used by VPNs. Since this
  // information is redundant with included routes, we plan to remove this
  // later.
  bool ipv4_default_route = true;
  std::vector<net_base::IPCIDR> excluded_route_prefixes;
  std::vector<net_base::IPCIDR> included_route_prefixes;

  // DNS and MTU configurations.
  std::vector<net_base::IPAddress> dns_servers;
  std::vector<std::string> dns_search_domains;
  std::optional<int> mtu;
};

bool operator==(const NetworkConfig& lhs, const NetworkConfig& rhs);

std::ostream& operator<<(std::ostream& stream, const NetworkConfig& config);

}  // namespace shill

#endif  // SHILL_NETWORK_NETWORK_CONFIG_H_
