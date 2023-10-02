// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/compound_network_config.h"

#include <memory>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <net-base/ipv4_address.h>

#include "shill/network/network_config.h"

namespace shill {

namespace {

class NetworkConfigMergeTest : public ::testing::Test {
 public:
  NetworkConfigMergeTest() {
    dhcp_config_.ipv4_address =
        net_base::IPv4CIDR::CreateFromCIDRString("192.168.1.101/24");
    dhcp_config_.ipv4_broadcast =
        net_base::IPv4Address::CreateFromString("192.168.1.255");
    dhcp_config_.ipv4_gateway =
        net_base::IPv4Address::CreateFromString("192.168.1.1");
    dhcp_config_.rfc3442_routes =
        std::vector<std::pair<net_base::IPv4CIDR, net_base::IPv4Address>>{
            {*net_base::IPv4CIDR::CreateFromCIDRString("10.1.0.0/16"),
             *net_base::IPv4Address::CreateFromString("192.168.1.2")}};
    dhcp_config_.dns_servers = {
        *net_base::IPAddress::CreateFromString("192.168.1.99"),
        *net_base::IPAddress::CreateFromString("192.168.1.98")};
    dhcp_config_.dns_search_domains = {"host1.domain", "host2.domain"};
    dhcp_config_.mtu = 1401;

    slaac_config_.ipv6_addresses = {
        *net_base::IPv6CIDR::CreateFromCIDRString("2001:db8:0:a::1001/64"),
        *net_base::IPv6CIDR::CreateFromCIDRString("2001:db8:0:a::1002/64")};
    slaac_config_.ipv6_gateway =
        net_base::IPv6Address::CreateFromString("fe80::cafe");
    slaac_config_.dns_servers = {
        *net_base::IPAddress::CreateFromString("2001:db8:0:1::1"),
        *net_base::IPAddress::CreateFromString("2001:db8:0:1::2")};
    slaac_config_.dns_search_domains = {"host1.domain", "host3.domain"};
    slaac_config_.mtu = 1402;
  }

 protected:
  NetworkConfig dhcp_config_;
  NetworkConfig slaac_config_;
};

TEST_F(NetworkConfigMergeTest, DHCPOnly) {
  CompoundNetworkConfig cnc("test_if");
  EXPECT_TRUE(cnc.SetFromDHCP(std::make_unique<NetworkConfig>(dhcp_config_)));
  EXPECT_EQ(dhcp_config_, cnc.Get());
}

TEST_F(NetworkConfigMergeTest, DHCPWithStatic) {
  CompoundNetworkConfig cnc("test_if");
  EXPECT_TRUE(cnc.SetFromDHCP(std::make_unique<NetworkConfig>(dhcp_config_)));
  EXPECT_EQ(dhcp_config_, cnc.Get());

  NetworkConfig static_config;
  EXPECT_FALSE(cnc.SetFromStatic(static_config));
  EXPECT_EQ(dhcp_config_, cnc.Get());

  static_config.ipv4_address =
      net_base::IPv4CIDR::CreateFromCIDRString("192.168.1.102/24");
  static_config.ipv4_gateway =
      net_base::IPv4Address::CreateFromString("192.168.1.2");
  static_config.dns_servers = {
      *net_base::IPAddress::CreateFromString("192.168.1.88"),
      *net_base::IPAddress::CreateFromString("192.168.1.87")};
  static_config.dns_search_domains = {"static1.domain", "static2.domain"};
  static_config.excluded_route_prefixes = {
      *net_base::IPCIDR::CreateFromCIDRString("172.16.2.0/24")};
  static_config.included_route_prefixes = {
      *net_base::IPCIDR::CreateFromCIDRString("172.16.3.0/24")};
  static_config.mtu = 1300;
  EXPECT_TRUE(cnc.SetFromStatic(static_config));
  EXPECT_EQ(static_config.ipv4_address, cnc.Get().ipv4_address);
  EXPECT_EQ(static_config.ipv4_broadcast, cnc.Get().ipv4_broadcast);
  EXPECT_EQ(static_config.ipv4_gateway, cnc.Get().ipv4_gateway);
  EXPECT_EQ(static_config.excluded_route_prefixes,
            cnc.Get().excluded_route_prefixes);
  EXPECT_EQ(static_config.included_route_prefixes,
            cnc.Get().included_route_prefixes);
  EXPECT_EQ(static_config.dns_servers, cnc.Get().dns_servers);
  EXPECT_EQ(static_config.dns_search_domains, cnc.Get().dns_search_domains);
  EXPECT_EQ(static_config.mtu, cnc.Get().mtu);
}

TEST_F(NetworkConfigMergeTest, SLAACOnly) {
  CompoundNetworkConfig cnc("test_if");
  EXPECT_TRUE(cnc.SetFromSLAAC(std::make_unique<NetworkConfig>(slaac_config_)));
  EXPECT_EQ(slaac_config_, cnc.Get());
}

TEST_F(NetworkConfigMergeTest, SLAACWithStatic) {
  CompoundNetworkConfig cnc("test_if");
  EXPECT_TRUE(cnc.SetFromSLAAC(std::make_unique<NetworkConfig>(slaac_config_)));
  EXPECT_EQ(slaac_config_, cnc.Get());

  // Static-configured DNS and DNSSL can get applied onto SLAAC IPv6-only
  // networks.
  NetworkConfig static_config;
  static_config.dns_servers = {
      *net_base::IPAddress::CreateFromString("2001:db8:0:2::1"),
      *net_base::IPAddress::CreateFromString("2001:db8:0:2::2")};
  static_config.dns_search_domains = {"static1.domain", "static2.domain"};
  EXPECT_TRUE(cnc.SetFromStatic(static_config));
  EXPECT_EQ(slaac_config_.ipv6_addresses, cnc.Get().ipv6_addresses);
  EXPECT_EQ(slaac_config_.ipv6_gateway, cnc.Get().ipv6_gateway);
  EXPECT_EQ(static_config.dns_servers, cnc.Get().dns_servers);
  EXPECT_EQ(static_config.dns_search_domains, cnc.Get().dns_search_domains);
  EXPECT_EQ(slaac_config_.mtu, cnc.Get().mtu);
}

TEST_F(NetworkConfigMergeTest, DHCPAndSLAAC) {
  CompoundNetworkConfig cnc("test_if");
  EXPECT_TRUE(cnc.SetFromSLAAC(std::make_unique<NetworkConfig>(slaac_config_)));
  EXPECT_TRUE(cnc.SetFromDHCP(std::make_unique<NetworkConfig>(dhcp_config_)));

  EXPECT_EQ(dhcp_config_.ipv4_address, cnc.Get().ipv4_address);
  EXPECT_EQ(dhcp_config_.ipv4_broadcast, cnc.Get().ipv4_broadcast);
  EXPECT_EQ(dhcp_config_.ipv4_gateway, cnc.Get().ipv4_gateway);
  EXPECT_EQ(dhcp_config_.excluded_route_prefixes,
            cnc.Get().excluded_route_prefixes);
  EXPECT_EQ(dhcp_config_.included_route_prefixes,
            cnc.Get().included_route_prefixes);
  EXPECT_EQ(slaac_config_.ipv6_addresses, cnc.Get().ipv6_addresses);
  EXPECT_EQ(slaac_config_.ipv6_gateway, cnc.Get().ipv6_gateway);
  EXPECT_EQ((std::vector<net_base::IPAddress>{
                *net_base::IPAddress::CreateFromString("2001:db8:0:1::1"),
                *net_base::IPAddress::CreateFromString("2001:db8:0:1::2"),
                *net_base::IPAddress::CreateFromString("192.168.1.99"),
                *net_base::IPAddress::CreateFromString("192.168.1.98")}),
            cnc.Get().dns_servers);
  EXPECT_EQ((std::vector<std::string>{"host1.domain", "host3.domain",
                                      "host2.domain"}),
            cnc.Get().dns_search_domains);
  EXPECT_EQ(1401, cnc.Get().mtu);  // Smaller value
}

TEST_F(NetworkConfigMergeTest, IPv4VPNWithStatic) {
  NetworkConfig vpn_config;
  vpn_config.ipv4_address =
      net_base::IPv4CIDR::CreateFromCIDRString("10.200.1.100/24");
  vpn_config.dns_servers = {
      *net_base::IPAddress::CreateFromString("10.200.0.2"),
      *net_base::IPAddress::CreateFromString("10.200.0.3")};
  vpn_config.ipv4_default_route = false;
  vpn_config.ipv6_blackhole_route = true;
  vpn_config.excluded_route_prefixes = {
      *net_base::IPCIDR::CreateFromCIDRString("172.16.2.0/24")};
  vpn_config.included_route_prefixes = {
      *net_base::IPCIDR::CreateFromCIDRString("172.16.3.0/24")};
  vpn_config.mtu = 1403;

  CompoundNetworkConfig cnc("test_if");
  EXPECT_TRUE(
      cnc.SetFromLinkProtocol(std::make_unique<NetworkConfig>(vpn_config)));
  EXPECT_EQ(vpn_config, cnc.Get());

  NetworkConfig static_config;
  static_config.dns_servers = {
      *net_base::IPAddress::CreateFromString("192.168.1.88"),
      *net_base::IPAddress::CreateFromString("192.168.1.87")};
  static_config.dns_search_domains = {"static1.domain", "static2.domain"};
  static_config.excluded_route_prefixes = {
      *net_base::IPCIDR::CreateFromCIDRString("172.16.2.0/24")};
  static_config.included_route_prefixes = {
      *net_base::IPCIDR::CreateFromCIDRString("172.16.3.0/24")};

  EXPECT_TRUE(cnc.SetFromStatic(static_config));
  EXPECT_EQ(vpn_config.ipv4_address, cnc.Get().ipv4_address);
  EXPECT_EQ(vpn_config.ipv4_broadcast, cnc.Get().ipv4_broadcast);
  EXPECT_EQ(vpn_config.ipv4_gateway, cnc.Get().ipv4_gateway);
  EXPECT_EQ(static_config.excluded_route_prefixes,
            cnc.Get().excluded_route_prefixes);
  EXPECT_EQ(static_config.included_route_prefixes,
            cnc.Get().included_route_prefixes);
  EXPECT_EQ(static_config.dns_servers, cnc.Get().dns_servers);
  EXPECT_EQ(static_config.dns_search_domains, cnc.Get().dns_search_domains);
  EXPECT_FALSE(cnc.Get().ipv4_default_route);
  EXPECT_TRUE(cnc.Get().ipv6_blackhole_route);
}

TEST_F(NetworkConfigMergeTest, CellWithStaticIPv6) {
  NetworkConfig cell_config;
  cell_config.ipv4_address =
      net_base::IPv4CIDR::CreateFromCIDRString("10.200.1.100/24");
  cell_config.ipv4_gateway =
      net_base::IPv4Address::CreateFromString("10.200.1.99");
  cell_config.ipv6_addresses = {
      *net_base::IPv6CIDR::CreateFromCIDRString("2001:db8:0:c::1001/64")};
  cell_config.ipv6_gateway =
      net_base::IPv6Address::CreateFromString("2001:db8:0:c::1000");
  cell_config.dns_servers = {
      *net_base::IPAddress::CreateFromString("2001:db8:0:cc::2"),
      *net_base::IPAddress::CreateFromString("2001:db8:0:cc::3"),
      *net_base::IPAddress::CreateFromString("10.200.0.2"),
      *net_base::IPAddress::CreateFromString("10.200.0.3"),
  };
  cell_config.mtu = 1403;

  CompoundNetworkConfig cnc("test_if");
  EXPECT_TRUE(
      cnc.SetFromLinkProtocol(std::make_unique<NetworkConfig>(cell_config)));
  EXPECT_EQ(cell_config, cnc.Get());
}

TEST_F(NetworkConfigMergeTest, CellWithDynamicIPv6) {
  NetworkConfig cell_config;
  cell_config.ipv4_address =
      net_base::IPv4CIDR::CreateFromCIDRString("10.200.1.100/24");
  cell_config.ipv4_gateway =
      net_base::IPv4Address::CreateFromString("10.200.1.99");
  cell_config.dns_servers = {
      *net_base::IPAddress::CreateFromString("10.200.0.2"),
      *net_base::IPAddress::CreateFromString("10.200.0.3")};
  cell_config.mtu = 1403;

  CompoundNetworkConfig cnc("test_if");
  EXPECT_TRUE(
      cnc.SetFromLinkProtocol(std::make_unique<NetworkConfig>(cell_config)));
  EXPECT_TRUE(cnc.SetFromSLAAC(std::make_unique<NetworkConfig>(slaac_config_)));

  EXPECT_EQ(cell_config.ipv4_address, cnc.Get().ipv4_address);
  EXPECT_EQ(cell_config.ipv4_broadcast, cnc.Get().ipv4_broadcast);
  EXPECT_EQ(cell_config.ipv4_gateway, cnc.Get().ipv4_gateway);
  EXPECT_EQ(slaac_config_.ipv6_addresses, cnc.Get().ipv6_addresses);
  EXPECT_EQ(slaac_config_.ipv6_gateway, cnc.Get().ipv6_gateway);
  EXPECT_EQ((std::vector<net_base::IPAddress>{
                *net_base::IPAddress::CreateFromString("2001:db8:0:1::1"),
                *net_base::IPAddress::CreateFromString("2001:db8:0:1::2"),
                *net_base::IPAddress::CreateFromString("10.200.0.2"),
                *net_base::IPAddress::CreateFromString("10.200.0.3")}),
            cnc.Get().dns_servers);
  EXPECT_EQ(slaac_config_.dns_search_domains, cnc.Get().dns_search_domains);
  EXPECT_EQ(1402, cnc.Get().mtu);  // Smaller value
}

}  // namespace

}  // namespace shill
