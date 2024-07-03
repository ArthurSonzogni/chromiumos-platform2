// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net-base/network_config.h"

#include <algorithm>

#include <base/containers/flat_set.h>
#include <base/strings/string_util.h>

#include "net-base/ipv4_address.h"
#include "net-base/ipv6_address.h"

namespace net_base {

NetworkConfig::NetworkConfig() = default;
NetworkConfig::~NetworkConfig() = default;

bool NetworkConfig::IsEmpty() const {
  return *this == NetworkConfig{};
}

bool NetworkConfig::operator==(const NetworkConfig& rhs) const = default;

// static
NetworkConfig NetworkConfig::Merge(const NetworkConfig* ipv4_config,
                                   const NetworkConfig* ipv6_config) {
  NetworkConfig ret;

  // IPv4 address/gateway configurations from |ipv4_config|.
  if (ipv4_config) {
    ret.ipv4_address = ipv4_config->ipv4_address;
    ret.ipv4_gateway = ipv4_config->ipv4_gateway;
    ret.ipv4_broadcast = ipv4_config->ipv4_broadcast;
    ret.ipv6_blackhole_route = ipv4_config->ipv6_blackhole_route;
    ret.rfc3442_routes.insert(ret.rfc3442_routes.end(),
                              ipv4_config->rfc3442_routes.begin(),
                              ipv4_config->rfc3442_routes.end());
  }

  // IPv6 address/gateway configurations from |ipv6_config|.
  if (ipv6_config) {
    ret.ipv6_addresses.insert(ret.ipv6_addresses.end(),
                              ipv6_config->ipv6_addresses.begin(),
                              ipv6_config->ipv6_addresses.end());
    ret.ipv6_gateway = ipv6_config->ipv6_gateway;
    ret.ipv6_delegated_prefixes = ipv6_config->ipv6_delegated_prefixes;
  }

  // Merge included routes and excluded routes from |ipv4_config| and
  // |ipv6_config|.
  for (const auto* config : {ipv4_config, ipv6_config}) {
    if (!config) {
      continue;
    }
    ret.included_route_prefixes.insert(ret.included_route_prefixes.end(),
                                       config->included_route_prefixes.begin(),
                                       config->included_route_prefixes.end());
    ret.excluded_route_prefixes.insert(ret.excluded_route_prefixes.end(),
                                       config->excluded_route_prefixes.begin(),
                                       config->excluded_route_prefixes.end());
  }

  // Merge DNS and DNSSL from |ipv4_config| and |ipv6_config|.
  base::flat_set<std::string_view> search_domains_dedup;
  // When DNS information is available from both IPv6 source and IPv4 source,
  // the ideal behavior is happy eyeballs (RFC 8305). When happy eyeballs is not
  // implemented, the priority of DNS servers are not strictly defined by
  // standard. Put IPv6 in front here as most of the RFCs just "assume" IPv6 is
  // preferred.
  for (const auto* config : {ipv6_config, ipv4_config}) {
    if (!config) {
      continue;
    }
    ret.dns_servers.insert(ret.dns_servers.end(), config->dns_servers.begin(),
                           config->dns_servers.end());
    for (const auto& domain : config->dns_search_domains) {
      if (!search_domains_dedup.contains(domain)) {
        ret.dns_search_domains.push_back(domain);
        search_domains_dedup.insert(domain);
      }
    }
  }

  // Merge MTU from |ipv4_config| and |ipv6_config|.
  int mtu = INT32_MAX;
  if (ipv4_config && ipv4_config->mtu.has_value()) {
    mtu = std::min(mtu, *ipv4_config->mtu);
  }
  if (ipv6_config && ipv6_config->mtu.has_value()) {
    mtu = std::min(mtu, *ipv6_config->mtu);
  }
  int min_mtu = ipv6_config ? net_base::NetworkConfig::kMinIPv6MTU
                            : net_base::NetworkConfig::kMinIPv4MTU;
  if (mtu < min_mtu) {
    mtu = min_mtu;
  }
  if (mtu != INT32_MAX) {
    ret.mtu = mtu;
  }

  // Merge captive portal URI from |ipv4_config| and |ipv6_config|.
  // Ideally the captive portal URI that comes first is used, but as we do not
  // know which one comes first here, we just prefer the one from IPv6 config
  // over IPv4.
  if (ipv6_config && ipv6_config->captive_portal_uri) {
    ret.captive_portal_uri = ipv6_config->captive_portal_uri;
  } else if (ipv4_config && ipv4_config->captive_portal_uri) {
    ret.captive_portal_uri = ipv4_config->captive_portal_uri;
  }
  return ret;
}

std::ostream& operator<<(std::ostream& stream, const NetworkConfig& config) {
  stream << "{IPv4 address: "
         << (config.ipv4_address.has_value() ? config.ipv4_address->ToString()
                                             : "nullopt");
  if (config.ipv4_broadcast) {
    stream << ", IPv4 broadcast: " << *config.ipv4_broadcast;
  }
  if (config.ipv4_gateway) {
    stream << ", IPv4 gateway: " << *config.ipv4_gateway;
  }
  stream << ", IPv6 addresses: [";
  std::vector<std::string> ipv6_address_str;
  std::transform(config.ipv6_addresses.begin(), config.ipv6_addresses.end(),
                 std::back_inserter(ipv6_address_str),
                 [](IPv6CIDR cidr) { return cidr.ToString(); });
  stream << base::JoinString(ipv6_address_str, ",");
  stream << "]";
  if (config.ipv6_gateway) {
    stream << ", IPv6 gateway: " << *config.ipv6_gateway;
  }
  if (!config.ipv6_delegated_prefixes.empty()) {
    stream << ", IPv6 delegated prefixes: [";
    std::vector<std::string> ipv6_pd_str;
    std::transform(config.ipv6_delegated_prefixes.begin(),
                   config.ipv6_delegated_prefixes.end(),
                   std::back_inserter(ipv6_pd_str),
                   [](IPv6CIDR cidr) { return cidr.ToString(); });
    stream << base::JoinString(ipv6_pd_str, ",");
    stream << "]";
  }
  if (config.ipv6_blackhole_route) {
    stream << ", blackhole IPv6";
  }
  if (!config.rfc3442_routes.empty()) {
    std::vector<std::string> rfc3442_routes_str;
    std::transform(config.rfc3442_routes.begin(), config.rfc3442_routes.end(),
                   std::back_inserter(rfc3442_routes_str),
                   [](std::pair<IPv4CIDR, IPv4Address> route) {
                     return route.first.ToString() + " via " +
                            route.second.ToString();
                   });
    stream << ", RFC 3442 classless static routes: ["
           << base::JoinString(rfc3442_routes_str, ",") << "]";
  }
  if (!config.excluded_route_prefixes.empty()) {
    std::vector<std::string> excluded_route_str;
    std::transform(config.excluded_route_prefixes.begin(),
                   config.excluded_route_prefixes.end(),
                   std::back_inserter(excluded_route_str),
                   [](IPCIDR cidr) { return cidr.ToString(); });
    stream << ", excluded routes: ["
           << base::JoinString(excluded_route_str, ",") << "]";
  }
  if (!config.included_route_prefixes.empty()) {
    std::vector<std::string> included_route_str;
    std::transform(config.included_route_prefixes.begin(),
                   config.included_route_prefixes.end(),
                   std::back_inserter(included_route_str),
                   [](IPCIDR cidr) { return cidr.ToString(); });
    stream << ", included routes: ["
           << base::JoinString(included_route_str, ",") << "]";
  }
  stream << ", DNS: [";
  std::vector<std::string> dns_str;
  std::transform(config.dns_servers.begin(), config.dns_servers.end(),
                 std::back_inserter(dns_str),
                 [](IPAddress dns) { return dns.ToString(); });
  stream << base::JoinString(dns_str, ",");
  stream << "]";
  if (!config.dns_search_domains.empty()) {
    stream << ", search domains: ["
           << base::JoinString(config.dns_search_domains, ",") << "]";
  }
  if (config.mtu) {
    stream << ", mtu: " << *config.mtu;
  }
  if (config.captive_portal_uri) {
    stream << ", captive_portal_uri: " << config.captive_portal_uri->ToString();
  }
  return stream << "}";
}

}  // namespace net_base
