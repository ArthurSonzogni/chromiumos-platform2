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
  auto ipv4_subnet =
      addr_mgr_->AllocateIPv4Subnet(AddressManager::GuestType::kParallelsVM, 0);

  CrostiniService::CrostiniDevice borealis_device(
      CrostiniService::VMType::kBorealis, "vmtap1", {}, std::move(ipv4_subnet),
      nullptr);

  BorealisVmStartupResponse proto;
  FillBorealisAllocationProto(borealis_device, &proto);
  ASSERT_EQ("vmtap1", proto.tap_device_ifname());
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

}  // namespace patchpanel
