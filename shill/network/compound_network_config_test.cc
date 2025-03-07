// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/compound_network_config.h"

#include <memory>
#include <utility>
#include <vector>

#include <chromeos/net-base/http_url.h>
#include <chromeos/net-base/ipv4_address.h>
#include <chromeos/net-base/network_config.h>
#include <gtest/gtest.h>

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
    dhcp_config_.captive_portal_uri =
        net_base::HttpUrl::CreateFromString("https://example.org/api/dhcp")
            .value();
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
    slaac_config_.captive_portal_uri =
        net_base::HttpUrl::CreateFromString("https://example.org/api/slaac")
            .value();
    slaac_config_.dns_search_domains = {"host1.domain", "host3.domain"};
    slaac_config_.mtu = 1402;
    slaac_config_.pref64 =
        *net_base::IPv6CIDR::CreateFromCIDRString("64:ff9b::/96");

    dhcppd_config_.ipv6_addresses = {
        *net_base::IPv6CIDR::CreateFromCIDRString("2001:db8:0:dd::2/128")};
    dhcppd_config_.ipv6_delegated_prefixes = {
        *net_base::IPv6CIDR::CreateFromCIDRString("2001:db8:0:dd::/64")};
    dhcppd_config_.mtu = 1403;

    google_dns_static_config_.dns_servers = {
        *net_base::IPAddress::CreateFromString("8.8.8.8"),
        *net_base::IPAddress::CreateFromString("8.8.4.4"),
        *net_base::IPAddress::CreateFromString("0.0.0.0"),
        *net_base::IPAddress::CreateFromString("0.0.0.0")};
  }

 protected:
  net_base::NetworkConfig dhcp_config_;
  net_base::NetworkConfig slaac_config_;
  net_base::NetworkConfig dhcppd_config_;
  net_base::NetworkConfig google_dns_static_config_;
};

TEST_F(NetworkConfigMergeTest, DHCPOnly) {
  CompoundNetworkConfig cnc("test_if");
  EXPECT_TRUE(
      cnc.SetFromDHCP(std::make_unique<net_base::NetworkConfig>(dhcp_config_)));
  EXPECT_EQ(dhcp_config_, cnc.Get());
}

TEST_F(NetworkConfigMergeTest, DHCPWithStatic) {
  const auto kNameServer1 =
      *net_base::IPAddress::CreateFromString("192.168.1.88");
  const auto kNameServer2 =
      *net_base::IPAddress::CreateFromString("192.168.1.87");
  const auto kNameServerEmpty =
      *net_base::IPAddress::CreateFromString("0.0.0.0");

  CompoundNetworkConfig cnc("test_if");
  EXPECT_TRUE(
      cnc.SetFromDHCP(std::make_unique<net_base::NetworkConfig>(dhcp_config_)));
  EXPECT_EQ(dhcp_config_, cnc.Get());

  net_base::NetworkConfig static_config;
  EXPECT_FALSE(cnc.SetFromStatic(static_config));
  EXPECT_EQ(dhcp_config_, cnc.Get());

  static_config.ipv4_address =
      net_base::IPv4CIDR::CreateFromCIDRString("192.168.1.102/24");
  static_config.ipv4_gateway =
      net_base::IPv4Address::CreateFromString("192.168.1.2");
  static_config.dns_servers = {
      kNameServer1,
      kNameServer2,
      // Empty servers should be trimmed.
      kNameServerEmpty,
      kNameServerEmpty,
  };
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
  EXPECT_EQ(std::vector<net_base::IPAddress>({kNameServer1, kNameServer2}),
            cnc.Get().dns_servers);
  EXPECT_EQ(static_config.dns_search_domains, cnc.Get().dns_search_domains);
  EXPECT_EQ(static_config.mtu, cnc.Get().mtu);

  EXPECT_EQ(dhcp_config_.captive_portal_uri, cnc.Get().captive_portal_uri);
}

TEST_F(NetworkConfigMergeTest, SLAACOnly) {
  CompoundNetworkConfig cnc("test_if");
  EXPECT_TRUE(cnc.SetFromSLAAC(
      std::make_unique<net_base::NetworkConfig>(slaac_config_)));
  EXPECT_EQ(slaac_config_, cnc.Get());
}

TEST_F(NetworkConfigMergeTest, SLAACWithStatic) {
  CompoundNetworkConfig cnc("test_if");
  EXPECT_TRUE(cnc.SetFromSLAAC(
      std::make_unique<net_base::NetworkConfig>(slaac_config_)));
  EXPECT_EQ(slaac_config_, cnc.Get());

  // Static-configured DNS and DNSSL can get applied onto SLAAC IPv6-only
  // networks.
  net_base::NetworkConfig static_config;
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
  EXPECT_EQ(slaac_config_.captive_portal_uri, cnc.Get().captive_portal_uri);
  EXPECT_EQ("64:ff9b::/96", cnc.Get().pref64->ToString());
}

TEST_F(NetworkConfigMergeTest, DHCPAndSLAAC) {
  CompoundNetworkConfig cnc("test_if");
  EXPECT_TRUE(cnc.SetFromSLAAC(
      std::make_unique<net_base::NetworkConfig>(slaac_config_)));
  EXPECT_TRUE(
      cnc.SetFromDHCP(std::make_unique<net_base::NetworkConfig>(dhcp_config_)));

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
  EXPECT_EQ("64:ff9b::/96", cnc.Get().pref64->ToString());

  // SLAAC config is set prior than DHCP, so use the value from SLAAC.
  // (Although in practice these two value should be the same).
  EXPECT_EQ(slaac_config_.captive_portal_uri, cnc.Get().captive_portal_uri);
}

TEST_F(NetworkConfigMergeTest, IPv4VPNWithStatic) {
  net_base::NetworkConfig vpn_config;
  vpn_config.ipv4_address =
      net_base::IPv4CIDR::CreateFromCIDRString("10.200.1.100/24");
  vpn_config.dns_servers = {
      *net_base::IPAddress::CreateFromString("10.200.0.2"),
      *net_base::IPAddress::CreateFromString("10.200.0.3")};
  vpn_config.ipv6_blackhole_route = true;
  vpn_config.excluded_route_prefixes = {
      *net_base::IPCIDR::CreateFromCIDRString("172.16.2.0/24")};
  vpn_config.included_route_prefixes = {
      *net_base::IPCIDR::CreateFromCIDRString("172.16.3.0/24")};
  vpn_config.mtu = 1403;

  CompoundNetworkConfig cnc("test_if");
  EXPECT_TRUE(cnc.SetFromLinkProtocol(
      std::make_unique<net_base::NetworkConfig>(vpn_config)));
  EXPECT_EQ(vpn_config, cnc.Get());

  net_base::NetworkConfig static_config;
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
  EXPECT_TRUE(cnc.Get().ipv6_blackhole_route);
}

TEST_F(NetworkConfigMergeTest, CellWithStaticIPv6) {
  net_base::NetworkConfig cell_config;
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
  EXPECT_TRUE(cnc.SetFromLinkProtocol(
      std::make_unique<net_base::NetworkConfig>(cell_config)));
  EXPECT_EQ(cell_config, cnc.Get());
}

TEST_F(NetworkConfigMergeTest, CellWithDynamicIPv6) {
  net_base::NetworkConfig cell_config;
  cell_config.ipv4_address =
      net_base::IPv4CIDR::CreateFromCIDRString("10.200.1.100/24");
  cell_config.ipv4_gateway =
      net_base::IPv4Address::CreateFromString("10.200.1.99");
  cell_config.dns_servers = {
      *net_base::IPAddress::CreateFromString("10.200.0.2"),
      *net_base::IPAddress::CreateFromString("10.200.0.3")};
  cell_config.mtu = 1403;

  CompoundNetworkConfig cnc("test_if");
  EXPECT_TRUE(cnc.SetFromLinkProtocol(
      std::make_unique<net_base::NetworkConfig>(cell_config)));
  EXPECT_TRUE(cnc.SetFromSLAAC(
      std::make_unique<net_base::NetworkConfig>(slaac_config_)));

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
  EXPECT_EQ("64:ff9b::/96", cnc.Get().pref64->ToString());
}

TEST_F(NetworkConfigMergeTest, DHCPAndDHCPPD) {
  CompoundNetworkConfig cnc("test_if");
  EXPECT_TRUE(cnc.SetFromSLAAC(
      std::make_unique<net_base::NetworkConfig>(slaac_config_)));
  EXPECT_TRUE(
      cnc.SetFromDHCP(std::make_unique<net_base::NetworkConfig>(dhcp_config_)));
  EXPECT_TRUE(cnc.SetFromDHCPv6(
      std::make_unique<net_base::NetworkConfig>(dhcppd_config_)));

  EXPECT_EQ(dhcp_config_.ipv4_address, cnc.Get().ipv4_address);
  EXPECT_EQ(dhcp_config_.ipv4_broadcast, cnc.Get().ipv4_broadcast);
  EXPECT_EQ(dhcp_config_.ipv4_gateway, cnc.Get().ipv4_gateway);
  EXPECT_EQ(dhcp_config_.excluded_route_prefixes,
            cnc.Get().excluded_route_prefixes);
  EXPECT_EQ(dhcp_config_.included_route_prefixes,
            cnc.Get().included_route_prefixes);
  EXPECT_EQ(dhcppd_config_.ipv6_addresses, cnc.Get().ipv6_addresses);
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
  EXPECT_EQ(dhcppd_config_.ipv6_delegated_prefixes,
            cnc.Get().ipv6_delegated_prefixes);
  EXPECT_EQ(1401, cnc.Get().mtu);  // Smaller value
  EXPECT_EQ("64:ff9b::/96", cnc.Get().pref64->ToString());

  // SLAAC config is set prior than DHCP, so use the value from SLAAC.
  // (Although in practice these two value should be the same).
  EXPECT_EQ(slaac_config_.captive_portal_uri, cnc.Get().captive_portal_uri);
}

TEST_F(NetworkConfigMergeTest, GoogleDnsListOnIPv6OnlyNetwork) {
  CompoundNetworkConfig cnc("test_if");
  EXPECT_TRUE(cnc.SetFromSLAAC(
      std::make_unique<net_base::NetworkConfig>(slaac_config_)));
  EXPECT_TRUE(cnc.SetFromStatic(google_dns_static_config_));

  EXPECT_EQ((std::vector<net_base::IPAddress>{
                *net_base::IPAddress::CreateFromString("2001:4860:4860::6464"),
                *net_base::IPAddress::CreateFromString("2001:4860:4860::64")}),
            cnc.Get().dns_servers);
}

}  // namespace

}  // namespace shill
