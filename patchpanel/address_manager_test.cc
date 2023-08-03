// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/address_manager.h"

#include <map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <base/containers/contains.h>
#include <base/containers/flat_set.h>
#include <base/rand_util.h>
#include <gtest/gtest.h>

namespace patchpanel {

using GuestType = AddressManager::GuestType;

TEST(AddressManager, BaseAddresses) {
  std::map<GuestType, net_base::IPv4Address> addrs = {
      {GuestType::kArc0, net_base::IPv4Address(100, 115, 92, 0)},
      {GuestType::kArcNet, net_base::IPv4Address(100, 115, 92, 4)},
      {GuestType::kTerminaVM, net_base::IPv4Address(100, 115, 92, 24)},
      {GuestType::kParallelsVM, net_base::IPv4Address(100, 115, 93, 0)},
      {GuestType::kLXDContainer, net_base::IPv4Address(100, 115, 92, 192)},
      {GuestType::kNetns, net_base::IPv4Address(100, 115, 92, 128)},
  };
  AddressManager mgr;
  for (const auto a : addrs) {
    auto subnet = mgr.AllocateIPv4Subnet(a.first);
    ASSERT_TRUE(subnet != nullptr);
    EXPECT_EQ(a.second, subnet->base_cidr().address());
  }
}

TEST(AddressManager, AddressesPerSubnet) {
  std::map<GuestType, size_t> addrs = {
      {GuestType::kArc0, 2},          {GuestType::kArcNet, 2},
      {GuestType::kTerminaVM, 2},     {GuestType::kParallelsVM, 6},
      {GuestType::kLXDContainer, 14}, {GuestType::kNetns, 2},
  };
  AddressManager mgr;
  for (const auto a : addrs) {
    auto subnet = mgr.AllocateIPv4Subnet(a.first);
    ASSERT_TRUE(subnet != nullptr);
    EXPECT_EQ(a.second, subnet->AvailableCount());
  }
}

TEST(AddressManager, SubnetsPerPool) {
  std::map<GuestType, size_t> addrs = {
      {GuestType::kArc0, 1},         {GuestType::kArcNet, 5},
      {GuestType::kTerminaVM, 26},   {GuestType::kParallelsVM, 32},
      {GuestType::kLXDContainer, 4}, {GuestType::kNetns, 16},
  };
  AddressManager mgr;
  for (const auto a : addrs) {
    std::vector<std::unique_ptr<Subnet>> subnets;
    for (size_t i = 0; i < a.second; ++i) {
      auto subnet = mgr.AllocateIPv4Subnet(a.first);
      EXPECT_TRUE(subnet != nullptr);
      subnets.emplace_back(std::move(subnet));
    }
    auto subnet = mgr.AllocateIPv4Subnet(a.first);
    EXPECT_TRUE(subnet == nullptr);
  }
}

TEST(AddressManager, SubnetIndexing) {
  AddressManager mgr;
  EXPECT_FALSE(mgr.AllocateIPv4Subnet(GuestType::kArc0, 1));
  EXPECT_FALSE(mgr.AllocateIPv4Subnet(GuestType::kArcNet, 1));
  EXPECT_FALSE(mgr.AllocateIPv4Subnet(GuestType::kTerminaVM, 1));
  EXPECT_TRUE(mgr.AllocateIPv4Subnet(GuestType::kParallelsVM, 1));
  EXPECT_FALSE(mgr.AllocateIPv4Subnet(GuestType::kLXDContainer, 1));
  EXPECT_FALSE(mgr.AllocateIPv4Subnet(GuestType::kNetns, 1));
}

TEST(AddressManager, StableMacAddresses) {
  AddressManager mgr;
  EXPECT_NE(mgr.GenerateMacAddress(), mgr.GenerateMacAddress());
  EXPECT_NE(mgr.GenerateMacAddress(kAnySubnetIndex),
            mgr.GenerateMacAddress(kAnySubnetIndex));
  for (int i = 0; i < 100; ++i) {
    uint8_t index = 0;
    while (index == 0) {
      base::RandBytes(&index, 1);
    }
    EXPECT_EQ(mgr.GenerateMacAddress(index), mgr.GenerateMacAddress(index));
  }
}

TEST(AddressManager, GenerateIPv6Subnet) {
  auto net_block = *net_base::IPv6CIDR::CreateFromStringAndPrefix("fd00::", 8);

  AddressManager mgr;
  for (int i = net_block.prefix_length() + 1; i <= 128; ++i) {
    auto subnet = mgr.GenerateIPv6Subnet(net_block, i);
    EXPECT_EQ(subnet.prefix_length(), i);
    EXPECT_EQ(subnet, subnet.GetPrefixCIDR());
    EXPECT_TRUE(net_block.InSameSubnetWith(subnet.address()));
  }
}

TEST(AddressManager, AllocateIPv6Subnet) {
  AddressManager mgr;
  base::flat_set<net_base::IPv6CIDR> subnets;
  for (int i = 0; i < 100; ++i) {
    auto subnet = mgr.AllocateIPv6Subnet();
    EXPECT_FALSE(base::Contains(subnets, subnet));
    subnets.insert(subnet);
  }
}

TEST(AddressManager, GetRandomizedIPv6Address) {
  AddressManager mgr;
  auto subnet = mgr.AllocateIPv6Subnet();
  for (int i = 0; i < 100; ++i) {
    auto cidr = mgr.GetRandomizedIPv6Address(subnet);
    EXPECT_TRUE(subnet.InSameSubnetWith(cidr.address()));
    EXPECT_EQ(subnet.prefix_length(), cidr.prefix_length());
  }
}

}  // namespace patchpanel
