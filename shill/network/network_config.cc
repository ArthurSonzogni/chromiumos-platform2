// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/network_config.h"

#include <algorithm>

#include <base/strings/string_util.h>
#include <net-base/ipv4_address.h>
#include <net-base/ipv6_address.h>

namespace shill {

NetworkConfig::NetworkConfig() = default;
NetworkConfig::~NetworkConfig() = default;

std::ostream& operator<<(std::ostream& stream, const NetworkConfig& config) {
  stream << "{IPv4 address: "
         << (config.ipv4_address.has_value() ? config.ipv4_address->ToString()
                                             : "nullopt");
  if (config.ipv4_broadcast) {
    stream << ", IPv4 broadcast: " << *config.ipv4_gateway;
  }
  if (config.ipv4_gateway) {
    stream << ", IPv4 gateway: " << *config.ipv4_gateway;
  }
  stream << ", IPv6 addresses: [";
  std::vector<std::string> ipv6_address_str;
  std::transform(config.ipv6_addresses.begin(), config.ipv6_addresses.end(),
                 std::back_inserter(ipv6_address_str),
                 [](net_base::IPv6CIDR cidr) { return cidr.ToString(); });
  stream << base::JoinString(ipv6_address_str, ",");
  stream << "]";
  if (config.ipv6_gateway) {
    stream << ", IPv6 gateway: " << *config.ipv6_gateway;
  }
  if (!config.ipv4_default_route) {
    stream << ", no IPv4 default route";
  }
  if (!config.excluded_route_prefixes.empty()) {
    std::vector<std::string> excluded_route_str;
    std::transform(config.excluded_route_prefixes.begin(),
                   config.excluded_route_prefixes.end(),
                   std::back_inserter(excluded_route_str),
                   [](net_base::IPCIDR cidr) { return cidr.ToString(); });
    stream << ", included routes: ["
           << base::JoinString(excluded_route_str, ",") << "]";
  }
  if (!config.included_route_prefixes.empty()) {
    std::vector<std::string> included_route_str;
    std::transform(config.included_route_prefixes.begin(),
                   config.included_route_prefixes.end(),
                   std::back_inserter(included_route_str),
                   [](net_base::IPCIDR cidr) { return cidr.ToString(); });
    stream << ", included routes: ["
           << base::JoinString(included_route_str, ",") << "]";
  }
  stream << ", DNS: [";
  std::vector<std::string> dns_str;
  std::transform(config.dns_servers.begin(), config.dns_servers.end(),
                 std::back_inserter(dns_str),
                 [](net_base::IPAddress dns) { return dns.ToString(); });
  stream << base::JoinString(dns_str, ",");
  stream << "]";
  if (!config.dns_search_domains.empty()) {
    stream << ", search domains: ["
           << base::JoinString(config.dns_search_domains, ",") << "]";
  }
  if (config.mtu) {
    stream << ", mtu: " << *config.mtu;
  }
  return stream << "}";
}

}  // namespace shill
