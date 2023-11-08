// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/dhcpv4_config.h"

#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <net-base/http_url.h>
#include <net-base/ipv4_address.h>

#include "shill/network/network_config.h"

namespace shill {

TEST(DHCPv4ConfigTest, ParseClasslessStaticRoutes) {
  const std::string kDefaultAddress = "0.0.0.0";
  const std::string kDefaultDestination = kDefaultAddress + "/0";
  const std::string kRouter0 = "10.0.0.254";
  const std::string kAddress1 = "192.168.1.0";
  const std::string kDestination1 = kAddress1 + "/24";
  // Last gateway missing, leaving an odd number of parameters.
  const std::string kBrokenClasslessRoutes0 =
      kDefaultDestination + " " + kRouter0 + " " + kDestination1;
  NetworkConfig network_config;
  EXPECT_FALSE(DHCPv4Config::ParseClasslessStaticRoutes(kBrokenClasslessRoutes0,
                                                        &network_config));
  EXPECT_TRUE(network_config.rfc3442_routes.empty());
  EXPECT_FALSE(network_config.ipv4_gateway.has_value());

  // Gateway argument for the second route is malformed, but we were able
  // to salvage a default gateway.
  const std::string kBrokenRouter1 = "10.0.0";
  const std::string kBrokenClasslessRoutes1 =
      kBrokenClasslessRoutes0 + " " + kBrokenRouter1;
  EXPECT_FALSE(DHCPv4Config::ParseClasslessStaticRoutes(kBrokenClasslessRoutes1,
                                                        &network_config));
  EXPECT_TRUE(network_config.rfc3442_routes.empty());
  EXPECT_EQ(*net_base::IPv4Address::CreateFromString(kRouter0),
            network_config.ipv4_gateway);

  const std::string kRouter1 = "10.0.0.253";
  const std::string kRouter2 = "10.0.0.252";
  const std::string kClasslessRoutes0 = kDefaultDestination + " " + kRouter2 +
                                        " " + kDestination1 + " " + kRouter1;
  EXPECT_TRUE(DHCPv4Config::ParseClasslessStaticRoutes(kClasslessRoutes0,
                                                       &network_config));
  // The old default route is preserved.
  EXPECT_EQ(*net_base::IPv4Address::CreateFromString(kRouter0),
            network_config.ipv4_gateway);

  // The two routes (including the one which would have otherwise been
  // classified as a default route) are added to the routing table.
  EXPECT_EQ(2, network_config.rfc3442_routes.size());
  const auto& route0 = network_config.rfc3442_routes[0];
  EXPECT_EQ(*net_base::IPv4CIDR::CreateFromStringAndPrefix(kDefaultAddress, 0),
            route0.first);
  EXPECT_EQ(*net_base::IPv4Address::CreateFromString(kRouter2), route0.second);

  const auto& route1 = network_config.rfc3442_routes[1];
  EXPECT_EQ(*net_base::IPv4CIDR::CreateFromStringAndPrefix(kAddress1, 24),
            route1.first);
  EXPECT_EQ(*net_base::IPv4Address::CreateFromString(kRouter1), route1.second);

  // A malformed routing table should not affect the current table.
  EXPECT_FALSE(DHCPv4Config::ParseClasslessStaticRoutes(kBrokenClasslessRoutes1,
                                                        &network_config));
  EXPECT_EQ(2, network_config.rfc3442_routes.size());
  EXPECT_EQ(*net_base::IPv4Address::CreateFromString(kRouter0),
            network_config.ipv4_gateway);
}

TEST(DHCPv4ConfigTest, ParseConfiguration) {
  KeyValueStore conf;
  conf.Set<uint32_t>(DHCPv4Config::kConfigurationKeyIPAddress, 0x01020304);
  conf.Set<uint8_t>(DHCPv4Config::kConfigurationKeySubnetCIDR, 16);
  conf.Set<uint32_t>(DHCPv4Config::kConfigurationKeyBroadcastAddress,
                     0x10203040);
  conf.Set<std::vector<uint32_t>>(DHCPv4Config::kConfigurationKeyRouters,
                                  {0x02040608, 0x03050709});
  conf.Set<std::vector<uint32_t>>(DHCPv4Config::kConfigurationKeyDNS,
                                  {0x09070503, 0x08060402});
  conf.Set<std::string>(DHCPv4Config::kConfigurationKeyDomainName,
                        "domain-name");
  conf.Set<Strings>(DHCPv4Config::kConfigurationKeyDomainSearch,
                    {"foo.com", "bar.com"});
  conf.Set<uint16_t>(DHCPv4Config::kConfigurationKeyMTU, 600);
  conf.Set<std::string>("UnknownKey", "UnknownValue");

  ByteArray isns_data{0x1, 0x2, 0x3, 0x4};
  conf.Set<std::vector<uint8_t>>(DHCPv4Config::kConfigurationKeyiSNSOptionData,
                                 isns_data);

  NetworkConfig network_config;
  DHCPv4Config::Data dhcp_data;
  ASSERT_TRUE(
      DHCPv4Config::ParseConfiguration(conf, &network_config, &dhcp_data));
  EXPECT_EQ(*net_base::IPv4CIDR::CreateFromStringAndPrefix("4.3.2.1", 16),
            network_config.ipv4_address);
  EXPECT_EQ(*net_base::IPv4Address::CreateFromString("64.48.32.16"),
            network_config.ipv4_broadcast);
  EXPECT_EQ(*net_base::IPv4Address::CreateFromString("8.6.4.2"),
            network_config.ipv4_gateway);
  ASSERT_EQ(2, network_config.dns_servers.size());
  EXPECT_EQ(*net_base::IPAddress::CreateFromString("3.5.7.9"),
            network_config.dns_servers[0]);
  EXPECT_EQ(*net_base::IPAddress::CreateFromString("2.4.6.8"),
            network_config.dns_servers[1]);
  ASSERT_EQ(2, network_config.dns_search_domains.size());
  EXPECT_EQ("foo.com", network_config.dns_search_domains[0]);
  EXPECT_EQ("bar.com", network_config.dns_search_domains[1]);
  EXPECT_EQ(600, network_config.mtu);
  EXPECT_EQ(isns_data.size(), dhcp_data.isns_option_data.size());
  EXPECT_FALSE(
      memcmp(&dhcp_data.isns_option_data[0], &isns_data[0], isns_data.size()));
}

TEST(DHCPv4ConfigTest, ParseConfigurationRespectingMinimumMTU) {
  // Values smaller than or equal to 576 should be ignored.
  for (int mtu = NetworkConfig::kMinIPv4MTU - 3;
       mtu < NetworkConfig::kMinIPv4MTU + 3; mtu++) {
    KeyValueStore conf;
    conf.Set<uint16_t>(DHCPv4Config::kConfigurationKeyMTU, mtu);
    NetworkConfig network_config;
    DHCPv4Config::Data dhcp_data;
    ASSERT_TRUE(
        DHCPv4Config::ParseConfiguration(conf, &network_config, &dhcp_data));
    if (mtu <= NetworkConfig::kMinIPv4MTU) {
      EXPECT_FALSE(network_config.mtu.has_value());
    } else {
      EXPECT_EQ(mtu, network_config.mtu);
    }
  }
}

TEST(DHCPv4ConfigTest, ParseConfigurationCaptivePortalUri) {
  const std::string kCaptivePortalUri = "https://example.org/portal.html";
  KeyValueStore conf;
  conf.Set<std::string>(DHCPv4Config::kConfigurationKeyCaptivePortalUri,
                        kCaptivePortalUri);

  NetworkConfig network_config;
  DHCPv4Config::Data dhcp_data;
  EXPECT_TRUE(
      DHCPv4Config::ParseConfiguration(conf, &network_config, &dhcp_data));
  EXPECT_EQ(network_config.captive_portal_uri,
            net_base::HttpUrl::CreateFromString(kCaptivePortalUri).value());
}

TEST(DHCPv4ConfigTest, ParseConfigurationCaptivePortalUriFailed) {
  const std::string kCaptivePortalUri = "invalid uri";
  KeyValueStore conf;
  conf.Set<std::string>(DHCPv4Config::kConfigurationKeyCaptivePortalUri,
                        kCaptivePortalUri);

  NetworkConfig network_config;
  DHCPv4Config::Data dhcp_data;
  EXPECT_FALSE(
      DHCPv4Config::ParseConfiguration(conf, &network_config, &dhcp_data));
  EXPECT_EQ(network_config.captive_portal_uri, std::nullopt);
}

}  // namespace shill
