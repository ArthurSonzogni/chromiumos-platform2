// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/proto_utils.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <base/functional/callback_helpers.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <metrics/metrics_library_mock.h>
#include <net-base/http_url.h>
#include <net-base/ipv4_address.h>
#include <net-base/ipv6_address.h>
#include <patchpanel/proto_bindings/patchpanel_service.pb.h>

#include "patchpanel/address_manager.h"
#include "patchpanel/arc_service.h"
#include "patchpanel/crostini_service.h"

namespace patchpanel {

class ProtoUtilsTest : public testing::Test {
 protected:
  void SetUp() override { addr_mgr_ = std::make_unique<AddressManager>(); }

  std::unique_ptr<AddressManager> addr_mgr_;
};

TEST_F(ProtoUtilsTest, FillTerminaAllocationProto) {
  const auto termina_ipv4_subnet =
      *net_base::IPv4CIDR::CreateFromCIDRString("100.115.92.24/30");
  const auto termina_ipv4_address =
      *net_base::IPv4Address::CreateFromString("100.115.92.26");
  const auto gateway_ipv4_address =
      *net_base::IPv4Address::CreateFromString("100.115.92.25");
  const auto container_ipv4_subnet =
      *net_base::IPv4CIDR::CreateFromCIDRString("100.115.92.192/28");
  const auto container_ipv4_address =
      *net_base::IPv4Address::CreateFromString("100.115.92.193");

  const uint32_t subnet_index = 0;
  const auto mac_addr = addr_mgr_->GenerateMacAddress(subnet_index);
  auto ipv4_subnet = addr_mgr_->AllocateIPv4Subnet(
      AddressManager::GuestType::kTerminaVM, subnet_index);
  auto lxd_subnet =
      addr_mgr_->AllocateIPv4Subnet(AddressManager::GuestType::kLXDContainer);
  auto termina_device = std::make_unique<CrostiniService::CrostiniDevice>(
      CrostiniService::VMType::kTermina, "vmtap0", mac_addr,
      std::move(ipv4_subnet), std::move(lxd_subnet));

  TerminaVmStartupResponse proto;
  FillTerminaAllocationProto(*termina_device, &proto);
  ASSERT_EQ("vmtap0", proto.tap_device_ifname());
  EXPECT_EQ(termina_ipv4_address,
            net_base::IPv4Address::CreateFromBytes(proto.ipv4_address()));
  EXPECT_EQ(gateway_ipv4_address, net_base::IPv4Address::CreateFromBytes(
                                      proto.gateway_ipv4_address()));
  EXPECT_EQ(termina_ipv4_subnet.address(),
            net_base::IPv4Address::CreateFromBytes(proto.ipv4_subnet().addr()));
  EXPECT_EQ(termina_ipv4_subnet.prefix_length(),
            proto.ipv4_subnet().prefix_len());
  EXPECT_EQ(container_ipv4_address, net_base::IPv4Address::CreateFromBytes(
                                        proto.container_ipv4_address()));
  EXPECT_EQ(container_ipv4_subnet.address(),
            net_base::IPv4Address::CreateFromBytes(
                proto.container_ipv4_subnet().addr()));
  EXPECT_EQ(container_ipv4_subnet.prefix_length(),
            proto.container_ipv4_subnet().prefix_len());
}

TEST_F(ProtoUtilsTest, FillParallelsAllocationProto) {
  const uint32_t subnet_index = 0;
  const auto parallels_ipv4_subnet =
      *net_base::IPv4CIDR::CreateFromCIDRString("100.115.93.0/29");
  const auto parallels_ipv4_address =
      *net_base::IPv4Address::CreateFromString("100.115.93.2");

  const auto mac_addr = addr_mgr_->GenerateMacAddress(subnet_index);
  auto ipv4_subnet = addr_mgr_->AllocateIPv4Subnet(
      AddressManager::GuestType::kParallelsVM, subnet_index);
  auto parallels_device = std::make_unique<CrostiniService::CrostiniDevice>(
      CrostiniService::VMType::kParallels, "vmtap1", mac_addr,
      std::move(ipv4_subnet), nullptr);

  ParallelsVmStartupResponse proto;
  FillParallelsAllocationProto(*parallels_device, &proto);
  ASSERT_EQ("vmtap1", proto.tap_device_ifname());
  EXPECT_EQ(parallels_ipv4_address,
            net_base::IPv4Address::CreateFromBytes(proto.ipv4_address()));
  EXPECT_EQ(parallels_ipv4_subnet.address(),
            net_base::IPv4Address::CreateFromBytes(proto.ipv4_subnet().addr()));
  EXPECT_EQ(parallels_ipv4_subnet.prefix_length(),
            proto.ipv4_subnet().prefix_len());
}

TEST_F(ProtoUtilsTest, FillBruschettaAllocationProto) {
  const auto bruschetta_ipv4_subnet =
      *net_base::IPv4CIDR::CreateFromCIDRString("100.115.93.0/29");
  const auto bruschetta_ipv4_address =
      *net_base::IPv4Address::CreateFromString("100.115.93.2");
  const auto gateway_ipv4_address =
      *net_base::IPv4Address::CreateFromString("100.115.93.1");
  auto ipv4_subnet =
      std::make_unique<Subnet>(bruschetta_ipv4_subnet, base::DoNothing());

  // TODO(b/279994478): Add kBruschetta at VMType.
  CrostiniService::CrostiniDevice bruschetta_device(
      CrostiniService::VMType::kParallels, "vmtap1", {}, std::move(ipv4_subnet),
      nullptr);

  BruschettaVmStartupResponse proto;
  FillBruschettaAllocationProto(bruschetta_device, &proto);
  ASSERT_EQ("vmtap1", proto.tap_device_ifname());
  EXPECT_EQ(bruschetta_ipv4_address,
            net_base::IPv4Address::CreateFromBytes(proto.ipv4_address()));
  EXPECT_EQ(gateway_ipv4_address, net_base::IPv4Address::CreateFromBytes(
                                      proto.gateway_ipv4_address()));
  EXPECT_EQ(bruschetta_ipv4_subnet.address(),
            net_base::IPv4Address::CreateFromBytes(proto.ipv4_subnet().addr()));
  EXPECT_EQ(bruschetta_ipv4_subnet.prefix_length(),
            proto.ipv4_subnet().prefix_len());
}

TEST_F(ProtoUtilsTest, FillBorealisAllocationProto) {
  const auto borealis_ipv4_subnet =
      *net_base::IPv4CIDR::CreateFromCIDRString("100.115.93.0/29");
  const auto borealis_ipv4_address =
      *net_base::IPv4Address::CreateFromString("100.115.93.2");
  const auto gateway_ipv4_address =
      *net_base::IPv4Address::CreateFromString("100.115.93.1");
  auto ipv4_subnet =
      std::make_unique<Subnet>(borealis_ipv4_subnet, base::DoNothing());

  CrostiniService::CrostiniDevice borealis_device(
      CrostiniService::VMType::kBorealis, "vmtap1", {}, std::move(ipv4_subnet),
      nullptr);

  BorealisVmStartupResponse proto;
  FillBorealisAllocationProto(borealis_device, &proto);
  ASSERT_EQ("vmtap1", proto.tap_device_ifname());
  EXPECT_EQ(borealis_ipv4_address,
            net_base::IPv4Address::CreateFromBytes(proto.ipv4_address()));
  EXPECT_EQ(gateway_ipv4_address, net_base::IPv4Address::CreateFromBytes(
                                      proto.gateway_ipv4_address()));
  EXPECT_EQ(borealis_ipv4_subnet.address(),
            net_base::IPv4Address::CreateFromBytes(proto.ipv4_subnet().addr()));
  EXPECT_EQ(borealis_ipv4_subnet.prefix_length(),
            proto.ipv4_subnet().prefix_len());
}

TEST_F(ProtoUtilsTest, FillNetworkClientInfoProto) {
  DownstreamClientInfo info;
  info.mac_addr = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
  info.ipv4_addr = net_base::IPv4Address(127, 0, 0, 1);
  info.ipv6_addresses.push_back(
      *net_base::IPv6Address::CreateFromString("fe80::1"));
  info.ipv6_addresses.push_back(
      *net_base::IPv6Address::CreateFromString("fe80::3"));
  info.hostname = "test_host";
  info.vendor_class = "test_vendor_class";

  NetworkClientInfo proto;
  FillNetworkClientInfoProto(info, &proto);

  EXPECT_EQ(proto.mac_addr(),
            std::string({0x11, 0x22, 0x33, 0x44, 0x55, 0x66}));
  EXPECT_EQ(proto.ipv4_addr(), std::string({127, 0, 0, 1}));
  EXPECT_EQ(proto.ipv6_addresses().size(), 2);
  EXPECT_EQ(proto.ipv6_addresses()[0],
            net_base::IPv6Address::CreateFromString("fe80::1")->ToByteString());
  EXPECT_EQ(proto.ipv6_addresses()[1],
            net_base::IPv6Address::CreateFromString("fe80::3")->ToByteString());
  EXPECT_EQ(proto.hostname(), "test_host");
  EXPECT_EQ(proto.vendor_class(), "test_vendor_class");
}

TEST_F(ProtoUtilsTest, DeserializeNetworkConfigEmpty) {
  patchpanel::NetworkConfig input;
  input.set_ipv4_default_route(true);

  const auto output = DeserializeNetworkConfig(input);
  net_base::NetworkConfig expected_output;
  EXPECT_EQ(output, expected_output);
}

TEST_F(ProtoUtilsTest, DeserializeNetworkConfig) {
  patchpanel::NetworkConfig input;
  auto* ipv4_address = input.mutable_ipv4_address();
  ipv4_address->set_addr({10, 0, 1, 100});
  ipv4_address->set_prefix_len(24);
  input.set_ipv4_gateway({10, 0, 1, 2});
  input.set_ipv4_broadcast({10, 0, 1, static_cast<char>(255)});
  auto* ipv6_address = input.add_ipv6_addresses();
  ipv6_address->set_addr(
      {0x20, 0x01, 0x2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x10, 0});
  ipv6_address->set_prefix_len(64);
  ipv6_address = input.add_ipv6_addresses();
  ipv6_address->set_addr(
      {0x20, 0x01, 0x2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x20, 0});
  ipv6_address->set_prefix_len(56);
  input.set_ipv6_gateway(
      {0x20, 0x01, 0x2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x2});
  input.set_ipv4_default_route(false);
  input.set_ipv6_blackhole_route(true);
  auto* prefix = input.add_excluded_route_prefixes();
  prefix->set_addr({0x20, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
  prefix->set_prefix_len(128);
  prefix = input.add_excluded_route_prefixes();
  prefix->set_addr({1, 1, 0, 0});
  prefix->set_prefix_len(32);
  prefix = input.add_included_route_prefixes();
  prefix->set_addr({0x20, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
  prefix->set_prefix_len(120);
  prefix = input.add_included_route_prefixes();
  prefix->set_addr({1, 1, 0, 0});
  prefix->set_prefix_len(28);
  auto* rfc3442_route = input.add_rfc3442_routes();
  auto* rfc3442_prefix = rfc3442_route->mutable_prefix();
  rfc3442_prefix->set_addr({2, 0, 0, 0});
  rfc3442_prefix->set_prefix_len(8);
  rfc3442_route->set_gateway({10, 0, 1, 3});
  input.add_dns_servers({8, 8, 8, 8});
  input.add_dns_servers({0x20, 0x01, 0x48, 0x60, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                         static_cast<char>(0x88), static_cast<char>(0x88)});
  input.add_dns_search_domains("google.com");
  input.set_mtu(1200);
  input.set_captive_portal_uri("https://portal.net");

  const auto output = DeserializeNetworkConfig(input);
  net_base::NetworkConfig expected_output;
  expected_output.ipv4_address =
      *net_base::IPv4CIDR::CreateFromCIDRString("10.0.1.100/24");
  expected_output.ipv4_gateway =
      *net_base::IPv4Address::CreateFromString("10.0.1.2");
  expected_output.ipv4_broadcast =
      *net_base::IPv4Address::CreateFromString("10.0.1.255");
  expected_output.ipv6_addresses.push_back(
      *net_base::IPv6CIDR::CreateFromCIDRString("2001:200::1000/64"));
  expected_output.ipv6_addresses.push_back(
      *net_base::IPv6CIDR::CreateFromCIDRString("2001:200::2000/56"));
  expected_output.ipv6_gateway =
      *net_base::IPv6Address::CreateFromString("2001:200::2");
  expected_output.ipv4_default_route = false;
  expected_output.ipv6_blackhole_route = true;
  expected_output.excluded_route_prefixes.push_back(
      *net_base::IPCIDR::CreateFromCIDRString("2002::/128"));
  expected_output.excluded_route_prefixes.push_back(
      *net_base::IPCIDR::CreateFromCIDRString("1.1.0.0/32"));
  expected_output.included_route_prefixes.push_back(
      *net_base::IPCIDR::CreateFromCIDRString("2002::/120"));
  expected_output.included_route_prefixes.push_back(
      *net_base::IPCIDR::CreateFromCIDRString("1.1.0.0/28"));
  expected_output.rfc3442_routes.emplace_back(
      *net_base::IPv4CIDR::CreateFromCIDRString("2.0.0.0/8"),
      *net_base::IPv4Address::CreateFromString("10.0.1.3"));
  expected_output.dns_servers.push_back(
      *net_base::IPAddress::CreateFromString("8.8.8.8"));
  expected_output.dns_servers.push_back(
      *net_base::IPAddress::CreateFromString("2001:4860::8888"));
  expected_output.dns_search_domains.push_back("google.com");
  expected_output.mtu = 1200;
  expected_output.captive_portal_uri =
      net_base::HttpUrl::CreateFromString("https://portal.net");

  EXPECT_EQ(output, expected_output);
}

}  // namespace patchpanel
