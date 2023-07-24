// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/downstream_network_service.h"

#include <optional>
#include <string>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <net-base/ipv4_address.h>
#include <patchpanel/proto_bindings/patchpanel_service.pb.h>

#include "patchpanel/dhcp_server_controller.h"
#include "patchpanel/shill_client.h"

using net_base::IPv4Address;
using net_base::IPv4CIDR;

using testing::_;
using testing::DoAll;
using testing::ElementsAre;
using testing::ElementsAreArray;
using testing::Mock;
using testing::Return;
using testing::SaveArg;
using testing::Sequence;
using testing::SetArgPointee;
using testing::StrEq;

namespace patchpanel {

TEST(DownstreamNetworkService,
     CreateDownstreamNetworkInfoFromTetheredNetworkRequest) {
  ShillClient::Device wwan0_dev;
  wwan0_dev.ifname = "wwan0";
  const auto ipv4_cidr = *IPv4CIDR::CreateFromCIDRString("192.168.3.1/24");
  const auto subnet_ip = ipv4_cidr.GetPrefixAddress();
  const IPv4Address start_ip = IPv4Address(192, 168, 3, 50);
  const IPv4Address end_ip = IPv4Address(192, 168, 3, 150);
  const std::vector<IPv4Address> dns_servers = {
      IPv4Address(1, 2, 3, 4),
      IPv4Address(5, 6, 7, 8),
  };
  const std::vector<std::string> domain_searches = {"domain.local0",
                                                    "domain.local1"};
  const int mtu = 1540;
  const DHCPServerController::Config::DHCPOptions dhcp_options = {
      {43, "ANDROID_METERED"}};

  IPv4Subnet* ipv4_subnet = new IPv4Subnet();
  ipv4_subnet->set_addr(subnet_ip.ToByteString());
  ipv4_subnet->set_prefix_len(
      static_cast<unsigned int>(ipv4_cidr.prefix_length()));

  IPv4Configuration* ipv4_config = new IPv4Configuration();
  ipv4_config->set_allocated_ipv4_subnet(ipv4_subnet);
  ipv4_config->set_gateway_addr(ipv4_cidr.address().ToByteString());
  ipv4_config->set_use_dhcp(true);
  ipv4_config->set_dhcp_start_addr(start_ip.ToByteString());
  ipv4_config->set_dhcp_end_addr(end_ip.ToByteString());
  ipv4_config->add_dns_servers(dns_servers[0].ToByteString());
  ipv4_config->add_dns_servers(dns_servers[1].ToByteString());
  ipv4_config->add_domain_searches(domain_searches[0]);
  ipv4_config->add_domain_searches(domain_searches[1]);
  auto dhcp_option = ipv4_config->add_options();
  dhcp_option->set_code(dhcp_options[0].first);
  dhcp_option->set_content(dhcp_options[0].second);

  TetheredNetworkRequest request;
  request.set_upstream_ifname("wwan0");
  request.set_ifname("wlan1");
  request.set_allocated_ipv4_config(ipv4_config);
  request.set_enable_ipv6(true);
  request.set_mtu(mtu);

  const auto info = DownstreamNetworkInfo::Create(request, wwan0_dev);
  ASSERT_NE(info, std::nullopt);
  EXPECT_EQ(info->topology, DownstreamNetworkTopology::kTethering);
  EXPECT_TRUE(info->upstream_device.has_value());
  EXPECT_EQ(info->upstream_device->ifname, "wwan0");
  EXPECT_EQ(info->downstream_ifname, "wlan1");
  EXPECT_EQ(info->ipv4_cidr, ipv4_cidr);
  EXPECT_EQ(info->ipv4_dhcp_start_addr, start_ip);
  EXPECT_EQ(info->ipv4_dhcp_end_addr, end_ip);
  EXPECT_EQ(info->dhcp_dns_servers, dns_servers);
  EXPECT_EQ(info->dhcp_domain_searches, domain_searches);
  EXPECT_EQ(info->mtu, mtu);
  EXPECT_EQ(info->enable_ipv6, true);
  EXPECT_EQ(info->dhcp_options, dhcp_options);
}

TEST(DownstreamNetworkService,
     CreateDownstreamNetworkInfoFromTetheredNetworkRequestRandom) {
  ShillClient::Device wwan0_dev;
  wwan0_dev.ifname = "wwan0";
  TetheredNetworkRequest request;
  request.set_upstream_ifname("wwan0");
  const auto info = DownstreamNetworkInfo::Create(request, wwan0_dev);
  ASSERT_NE(info, std::nullopt);

  // When the request doesn't have |ipv4_config|, the info should be randomly
  // assigned the valid host IP and DHCP range.
  EXPECT_TRUE(info->ipv4_cidr.InSameSubnetWith(info->ipv4_dhcp_start_addr));
  EXPECT_TRUE(info->ipv4_cidr.InSameSubnetWith(info->ipv4_dhcp_end_addr));
}

TEST(DownstreamNetworkService,
     CreateDownstreamNetworkInfoFromLocalOnlyNetworkRequest) {
  LocalOnlyNetworkRequest request;
  request.set_ifname("wlan1");

  const auto info = DownstreamNetworkInfo::Create(request);
  ASSERT_NE(info, std::nullopt);
  EXPECT_EQ(info->topology, DownstreamNetworkTopology::kLocalOnly);
  EXPECT_EQ(info->downstream_ifname, "wlan1");
}

TEST(DownstreamNetworkService, DownstreamNetworkInfoToDHCPServerConfig) {
  DownstreamNetworkInfo info = {};
  info.ipv4_cidr = *IPv4CIDR::CreateFromCIDRString("192.168.3.1/24");
  info.enable_ipv4_dhcp = true;
  info.ipv4_dhcp_start_addr = IPv4Address(192, 168, 3, 50);
  info.ipv4_dhcp_end_addr = IPv4Address(192, 168, 3, 100);
  info.dhcp_dns_servers.push_back(IPv4Address(1, 2, 3, 4));
  info.dhcp_dns_servers.push_back(IPv4Address(5, 6, 7, 8));
  info.dhcp_domain_searches.push_back("domain.local0");
  info.dhcp_domain_searches.push_back("domain.local1");
  info.mtu = 1450;

  const auto config = info.ToDHCPServerConfig();
  ASSERT_NE(config, std::nullopt);
  EXPECT_EQ(config->host_ip(), "192.168.3.1");
  EXPECT_EQ(config->netmask(), "255.255.255.0");
  EXPECT_EQ(config->start_ip(), "192.168.3.50");
  EXPECT_EQ(config->end_ip(), "192.168.3.100");
  EXPECT_EQ(config->dns_servers(), "1.2.3.4,5.6.7.8");
  EXPECT_EQ(config->domain_searches(), "domain.local0,domain.local1");
  EXPECT_EQ(config->mtu(), "1450");
}
}  // namespace patchpanel
