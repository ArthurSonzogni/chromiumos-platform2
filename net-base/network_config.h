// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_BASE_NETWORK_CONFIG_H_
#define NET_BASE_NETWORK_CONFIG_H_

#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include <brillo/brillo_export.h>

#include "net-base/http_url.h"
#include "net-base/ip_address.h"
#include "net-base/ipv4_address.h"
#include "net-base/ipv6_address.h"

namespace net_base {

// Properties related to the IP layer used to represent a configuration.
struct BRILLO_EXPORT NetworkConfig {
  // Define a default and the minimum viable MTU values.
  static constexpr int kDefaultMTU = 1500;
  static constexpr int kMinIPv4MTU = 576;
  static constexpr int kMinIPv6MTU = 1280;

  NetworkConfig();
  ~NetworkConfig();

  bool operator==(const NetworkConfig& rhs) const;
  bool IsEmpty() const;

  // Creates a new NetworkConfig with IPv4 properties from |ipv4_config| and
  // IPv6 properties from |ipv6_config|. Non-family-specific fields are merged.
  static NetworkConfig Merge(const NetworkConfig* ipv4_config,
                             const NetworkConfig* ipv6_config);

  // IPv4 configurations. If |ipv4_address| is null, no IPv4 is configured on
  // the Network. If |ipv4_address| is present but |ipv4_gateway| is null,
  // routes are to be added on-link to the netdevice.
  std::optional<IPv4CIDR> ipv4_address;
  std::optional<IPv4Address> ipv4_broadcast;
  std::optional<IPv4Address> ipv4_gateway;

  // IPv6 configurations. If |ipv6_gateway| is null, routes are to be added
  // on-link to the netdevice.
  std::vector<IPv6CIDR> ipv6_addresses;
  std::optional<IPv6Address> ipv6_gateway;
  // Prefixes assigned through DHCPv6-PD. Note these prefixes will not be
  // directly used for host configuration, unless an address in the prefix is
  // also explicitly included in |ipv6_addresses|.
  std::vector<IPv6CIDR> ipv6_delegated_prefixes;

  // Routing configurations. If a destination is included, it will be routed
  // through the gateway of corresponding IP family (or on-link if gateway is
  // null).
  // If true a IPv6 default blackhole route is added to aggressively block IPv6
  // traffic. Used if connected to an IPv4-only VPN.
  bool ipv6_blackhole_route = false;
  std::vector<IPCIDR> excluded_route_prefixes;
  std::vector<IPCIDR> included_route_prefixes;
  std::vector<std::pair<IPv4CIDR, IPv4Address>> rfc3442_routes;
  std::optional<net_base::IPv6CIDR> pref64;

  // DNS and MTU configurations.
  std::vector<IPAddress> dns_servers;
  std::vector<std::string> dns_search_domains;
  std::optional<int> mtu;

  // The captive portal URI gained from DHCP option 114, defined at RFC 8910.
  std::optional<HttpUrl> captive_portal_uri;
};

BRILLO_EXPORT std::ostream& operator<<(std::ostream& stream,
                                       const NetworkConfig& config);

}  // namespace net_base

#endif  // NET_BASE_NETWORK_CONFIG_H_
