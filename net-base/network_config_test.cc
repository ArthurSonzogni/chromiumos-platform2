// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net-base/network_config.h"

#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "net-base/http_url.h"
#include "net-base/ip_address.h"
#include "net-base/ipv4_address.h"
#include "net-base/ipv6_address.h"

using testing::Test;

namespace net_base {

TEST(NetworkConfigTest, MergeAddress) {
  NetworkConfig ipv4_config;
  ipv4_config.ipv4_address = IPv4CIDR::CreateFromCIDRString("192.168.1.0/24");
  ipv4_config.ipv4_gateway = IPv4Address::CreateFromString("192.168.1.1");
  ipv4_config.ipv4_broadcast = IPv4Address::CreateFromString("192.168.1.255");
  ipv4_config.rfc3442_routes =
      std::vector<std::pair<net_base::IPv4CIDR, net_base::IPv4Address>>{
          {*net_base::IPv4CIDR::CreateFromCIDRString("10.1.0.0/16"),
           *net_base::IPv4Address::CreateFromString("192.168.1.2")}};
  NetworkConfig ipv6_config;
  ipv6_config.ipv6_addresses = {*IPv6CIDR::CreateFromCIDRString("fd00::/64")};
  ipv6_config.ipv6_gateway = IPv6Address::CreateFromString("fd00::1");

  NetworkConfig merged_config =
      NetworkConfig::Merge(&ipv4_config, &ipv6_config);
  EXPECT_EQ(merged_config.ipv4_address, ipv4_config.ipv4_address);
  EXPECT_EQ(merged_config.ipv4_gateway, ipv4_config.ipv4_gateway);
  EXPECT_EQ(merged_config.ipv4_broadcast, ipv4_config.ipv4_broadcast);
  EXPECT_EQ(merged_config.ipv6_blackhole_route,
            ipv4_config.ipv6_blackhole_route);
  EXPECT_EQ(merged_config.rfc3442_routes, ipv4_config.rfc3442_routes);
  EXPECT_EQ(merged_config.ipv6_addresses, ipv6_config.ipv6_addresses);
  EXPECT_EQ(merged_config.ipv6_gateway, ipv6_config.ipv6_gateway);
}

TEST(NetworkConfigTest, MergeIncludedExcludedRoutes) {
  NetworkConfig ipv4_config;
  ipv4_config.included_route_prefixes = {
      *IPCIDR::CreateFromCIDRString("10.1.0.0/16")};
  ipv4_config.excluded_route_prefixes = {
      *IPCIDR::CreateFromCIDRString("10.2.0.0/16")};
  NetworkConfig ipv6_config;
  ipv6_config.included_route_prefixes = {
      *IPCIDR::CreateFromCIDRString("fd01::/16"),
      *IPCIDR::CreateFromCIDRString("fd02::/16")};
  ipv6_config.excluded_route_prefixes = {
      *IPCIDR::CreateFromCIDRString("fd03::/16")};

  NetworkConfig merged_config = NetworkConfig::Merge(nullptr, &ipv6_config);
  EXPECT_EQ(merged_config.included_route_prefixes,
            ipv6_config.included_route_prefixes);
  EXPECT_EQ(merged_config.excluded_route_prefixes,
            ipv6_config.excluded_route_prefixes);

  merged_config = NetworkConfig::Merge(&ipv4_config, &ipv6_config);
  EXPECT_EQ(merged_config.included_route_prefixes,
            (std::vector<IPCIDR>{ipv4_config.included_route_prefixes[0],
                                 ipv6_config.included_route_prefixes[0],
                                 ipv6_config.included_route_prefixes[1]}));
  EXPECT_EQ(merged_config.excluded_route_prefixes,
            (std::vector<IPCIDR>{ipv4_config.excluded_route_prefixes[0],
                                 ipv6_config.excluded_route_prefixes[0]}));
}

TEST(NetworkConfigTest, MergeDNS) {
  NetworkConfig ipv4_config;
  ipv4_config.dns_servers = {*IPAddress::CreateFromString("8.8.8.8")};
  ipv4_config.dns_search_domains = {"example1.com", "example2.com"};
  NetworkConfig ipv6_config;
  ipv6_config.dns_servers = {
      *IPAddress::CreateFromString("2001:4860:4860:0:0:0:0:8888")};
  ipv6_config.dns_search_domains = {"example2.com", "example3.com"};

  NetworkConfig merged_config =
      NetworkConfig::Merge(&ipv4_config, &ipv6_config);
  EXPECT_EQ(merged_config.dns_servers,
            (std::vector<IPAddress>{ipv6_config.dns_servers[0],
                                    ipv4_config.dns_servers[0]}));
  EXPECT_EQ(merged_config.dns_search_domains,
            (std::vector<std::string>{"example2.com", "example3.com",
                                      "example1.com"}));
}

TEST(NetworkConfigTest, MergeMTU) {
  NetworkConfig ipv4_config;
  ipv4_config.mtu = 1000;  // less than |kMinIPv6MTU|
  NetworkConfig ipv6_config;
  ipv6_config.mtu = 1500;

  NetworkConfig merged_config = NetworkConfig::Merge(&ipv4_config, nullptr);
  EXPECT_EQ(merged_config.mtu, ipv4_config.mtu);
  merged_config = NetworkConfig::Merge(nullptr, &ipv6_config);
  EXPECT_EQ(merged_config.mtu, ipv6_config.mtu);
  // The minimum MTU for dual stack NetworkConfig is |kMinIPv6MTU|.
  merged_config = NetworkConfig::Merge(&ipv4_config, &ipv6_config);
  EXPECT_EQ(merged_config.mtu, NetworkConfig::kMinIPv6MTU);
}

TEST(NetworkConfigTest, MergeCaptivePortalURI) {
  NetworkConfig ipv4_config;
  ipv4_config.captive_portal_uri =
      HttpUrl::CreateFromString("https://example.org/api/v4");
  NetworkConfig ipv6_config;
  ipv6_config.captive_portal_uri =
      HttpUrl::CreateFromString("https://example.org/api/v6");

  NetworkConfig merged_config = NetworkConfig::Merge(&ipv4_config, nullptr);
  EXPECT_EQ(merged_config.captive_portal_uri, ipv4_config.captive_portal_uri);
  merged_config = NetworkConfig::Merge(&ipv4_config, &ipv6_config);
  EXPECT_EQ(merged_config.captive_portal_uri, ipv6_config.captive_portal_uri);
}
}  // namespace net_base
