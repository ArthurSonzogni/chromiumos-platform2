// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/subnet.h"

#include <arpa/inet.h>
#include <stdint.h>

#include <string>
#include <utility>
#include <vector>

#include <base/functional/bind.h>
#include <base/functional/callback_helpers.h>
#include <base/strings/string_util.h>
#include <gtest/gtest.h>

#include "patchpanel/net_util.h"

namespace patchpanel {
namespace {

constexpr uint32_t kContainerBaseAddress = Ipv4Addr(100, 115, 92, 192);
constexpr uint32_t kVmBaseAddress = Ipv4Addr(100, 115, 92, 24);
constexpr uint32_t kParallelsBaseAddress = Ipv4Addr(100, 115, 92, 128);

constexpr uint32_t kContainerSubnetPrefixLength = 28;
constexpr uint32_t kVmSubnetPrefixLength = 30;
constexpr uint32_t kParallelsSubnetPrefixLength = 28;

uint32_t AddOffset(uint32_t base_addr_no, uint32_t offset_ho) {
  return htonl(ntohl(base_addr_no) + offset_ho);
}

// kExpectedAvailableCount[i] == AvailableCount() for subnet with prefix_length
// i.
constexpr uint32_t kExpectedAvailableCount[] = {
    0xfffffffe, 0x7ffffffe, 0x3ffffffe, 0x1ffffffe, 0xffffffe, 0x7fffffe,
    0x3fffffe,  0x1fffffe,  0xfffffe,   0x7ffffe,   0x3ffffe,  0x1ffffe,
    0xffffe,    0x7fffe,    0x3fffe,    0x1fffe,    0xfffe,    0x7ffe,
    0x3ffe,     0x1ffe,     0xffe,      0x7fe,      0x3fe,     0x1fe,
    0xfe,       0x7e,       0x3e,       0x1e,       0xe,       0x6,
    0x2,        0x0,
};

// kExpectedNetmask[i] == Netmask() for subnet with prefix_length i.
constexpr uint32_t kExpectedNetmask[] = {
    Ipv4Addr(0, 0, 0, 0),         Ipv4Addr(128, 0, 0, 0),
    Ipv4Addr(192, 0, 0, 0),       Ipv4Addr(224, 0, 0, 0),
    Ipv4Addr(240, 0, 0, 0),       Ipv4Addr(248, 0, 0, 0),
    Ipv4Addr(252, 0, 0, 0),       Ipv4Addr(254, 0, 0, 0),
    Ipv4Addr(255, 0, 0, 0),       Ipv4Addr(255, 128, 0, 0),
    Ipv4Addr(255, 192, 0, 0),     Ipv4Addr(255, 224, 0, 0),
    Ipv4Addr(255, 240, 0, 0),     Ipv4Addr(255, 248, 0, 0),
    Ipv4Addr(255, 252, 0, 0),     Ipv4Addr(255, 254, 0, 0),
    Ipv4Addr(255, 255, 0, 0),     Ipv4Addr(255, 255, 128, 0),
    Ipv4Addr(255, 255, 192, 0),   Ipv4Addr(255, 255, 224, 0),
    Ipv4Addr(255, 255, 240, 0),   Ipv4Addr(255, 255, 248, 0),
    Ipv4Addr(255, 255, 252, 0),   Ipv4Addr(255, 255, 254, 0),
    Ipv4Addr(255, 255, 255, 0),   Ipv4Addr(255, 255, 255, 128),
    Ipv4Addr(255, 255, 255, 192), Ipv4Addr(255, 255, 255, 224),
    Ipv4Addr(255, 255, 255, 240), Ipv4Addr(255, 255, 255, 248),
    Ipv4Addr(255, 255, 255, 252), Ipv4Addr(255, 255, 255, 254),
};

// kExpectedPrefix[i] == Prefix() for subnet with 4 * i offset to
// |kVmBaseAddress|.
constexpr uint32_t kExpectedPrefix[] = {
    Ipv4Addr(100, 115, 92, 24),  Ipv4Addr(100, 115, 92, 28),
    Ipv4Addr(100, 115, 92, 32),  Ipv4Addr(100, 115, 92, 36),
    Ipv4Addr(100, 115, 92, 40),  Ipv4Addr(100, 115, 92, 44),
    Ipv4Addr(100, 115, 92, 48),  Ipv4Addr(100, 115, 92, 52),
    Ipv4Addr(100, 115, 92, 56),  Ipv4Addr(100, 115, 92, 60),
    Ipv4Addr(100, 115, 92, 64),  Ipv4Addr(100, 115, 92, 68),
    Ipv4Addr(100, 115, 92, 72),  Ipv4Addr(100, 115, 92, 76),
    Ipv4Addr(100, 115, 92, 80),  Ipv4Addr(100, 115, 92, 84),
    Ipv4Addr(100, 115, 92, 88),  Ipv4Addr(100, 115, 92, 92),
    Ipv4Addr(100, 115, 92, 96),  Ipv4Addr(100, 115, 92, 100),
    Ipv4Addr(100, 115, 92, 104), Ipv4Addr(100, 115, 92, 108),
    Ipv4Addr(100, 115, 92, 112), Ipv4Addr(100, 115, 92, 116),
    Ipv4Addr(100, 115, 92, 120), Ipv4Addr(100, 115, 92, 124),
    Ipv4Addr(100, 115, 92, 128), Ipv4Addr(100, 115, 92, 132),
    Ipv4Addr(100, 115, 92, 136), Ipv4Addr(100, 115, 92, 140),
    Ipv4Addr(100, 115, 92, 144), Ipv4Addr(100, 115, 92, 148),
};

// kExpectedCidrString[i] == ToCidrString() for subnet with 4 * i offset to
// |kVmBaseAddress|.
const char* kExpectedCidrString[] = {
    "100.115.92.24/30",  "100.115.92.28/30",  "100.115.92.32/30",
    "100.115.92.36/30",  "100.115.92.40/30",  "100.115.92.44/30",
    "100.115.92.48/30",  "100.115.92.52/30",  "100.115.92.56/30",
    "100.115.92.60/30",  "100.115.92.64/30",  "100.115.92.68/30",
    "100.115.92.72/30",  "100.115.92.76/30",  "100.115.92.80/30",
    "100.115.92.84/30",  "100.115.92.88/30",  "100.115.92.92/30",
    "100.115.92.96/30",  "100.115.92.100/30", "100.115.92.104/30",
    "100.115.92.108/30", "100.115.92.112/30", "100.115.92.116/30",
    "100.115.92.120/30", "100.115.92.124/30", "100.115.92.128/30",
    "100.115.92.132/30", "100.115.92.136/30", "100.115.92.140/30",
    "100.115.92.144/30", "100.115.92.148/30",
};

class VmSubnetTest : public ::testing::TestWithParam<uint32_t> {};
class ContainerSubnetTest : public ::testing::TestWithParam<uint32_t> {};
class PrefixTest : public ::testing::TestWithParam<int> {};

void SetTrue(bool* value) {
  *value = true;
}

}  // namespace

TEST_P(VmSubnetTest, Prefix) {
  uint32_t index = GetParam();
  Subnet subnet(
      *net_base::IPv4CIDR::CreateFromAddressAndPrefix(
          ConvertUint32ToIPv4Address(AddOffset(kVmBaseAddress, index * 4)),
          kVmSubnetPrefixLength),
      base::DoNothing());

  EXPECT_EQ(kExpectedPrefix[index], subnet.Prefix());
}

TEST_P(VmSubnetTest, CidrString) {
  uint32_t index = GetParam();
  Subnet subnet(
      *net_base::IPv4CIDR::CreateFromAddressAndPrefix(
          ConvertUint32ToIPv4Address(AddOffset(kVmBaseAddress, index * 4)),
          kVmSubnetPrefixLength),
      base::DoNothing());

  EXPECT_EQ(std::string(kExpectedCidrString[index]), subnet.ToCidrString());
  EXPECT_EQ(kExpectedCidrString[index], subnet.ToCidrString());
}

TEST_P(VmSubnetTest, AddressAtOffset) {
  uint32_t index = GetParam();
  Subnet subnet(
      *net_base::IPv4CIDR::CreateFromAddressAndPrefix(
          ConvertUint32ToIPv4Address(AddOffset(kVmBaseAddress, index * 4)),
          kVmSubnetPrefixLength),
      base::DoNothing());

  for (uint32_t offset = 1; offset <= subnet.AvailableCount(); ++offset) {
    uint32_t address = AddOffset(kVmBaseAddress, index * 4 + offset);
    EXPECT_EQ(address, subnet.AddressAtOffset(offset));
  }
}

INSTANTIATE_TEST_SUITE_P(AllValues,
                         VmSubnetTest,
                         ::testing::Range(uint32_t{0}, uint32_t{26}));

TEST_P(ContainerSubnetTest, AddressAtOffset) {
  uint32_t index = GetParam();
  Subnet subnet(*net_base::IPv4CIDR::CreateFromAddressAndPrefix(
                    ConvertUint32ToIPv4Address(
                        AddOffset(kContainerBaseAddress, index * 16)),
                    kContainerSubnetPrefixLength),
                base::DoNothing());

  for (uint32_t offset = 1; offset <= subnet.AvailableCount(); ++offset) {
    uint32_t address = AddOffset(kContainerBaseAddress, index * 16 + offset);
    EXPECT_EQ(address, subnet.AddressAtOffset(offset));
  }
}

INSTANTIATE_TEST_SUITE_P(AllValues,
                         ContainerSubnetTest,
                         ::testing::Range(uint32_t{1}, uint32_t{4}));

TEST_P(PrefixTest, AvailableCount) {
  int prefix_length = GetParam();

  Subnet subnet(
      *net_base::IPv4CIDR::CreateFromAddressAndPrefix({}, prefix_length),
      base::DoNothing());
  EXPECT_EQ(kExpectedAvailableCount[prefix_length], subnet.AvailableCount());
}

TEST_P(PrefixTest, Netmask) {
  int prefix_length = GetParam();

  Subnet subnet(
      *net_base::IPv4CIDR::CreateFromAddressAndPrefix({}, prefix_length),
      base::DoNothing());
  EXPECT_EQ(kExpectedNetmask[prefix_length], subnet.Netmask());
}

INSTANTIATE_TEST_SUITE_P(AllValues, PrefixTest, ::testing::Range(8, 32));

TEST(SubtnetAddress, StringConversion) {
  Subnet container_subnet(*net_base::IPv4CIDR::CreateFromAddressAndPrefix(
                              ConvertUint32ToIPv4Address(kContainerBaseAddress),
                              kContainerSubnetPrefixLength),
                          base::DoNothing());
  EXPECT_EQ("100.115.92.192/28", container_subnet.ToCidrString());
  {
    EXPECT_EQ(*net_base::IPv4CIDR::CreateFromCIDRString("100.115.92.193/28"),
              container_subnet.AllocateAtOffset(1)->cidr());
    EXPECT_EQ(*net_base::IPv4CIDR::CreateFromCIDRString("100.115.92.194/28"),
              container_subnet.AllocateAtOffset(2)->cidr());
    EXPECT_EQ(*net_base::IPv4CIDR::CreateFromCIDRString("100.115.92.205/28"),
              container_subnet.AllocateAtOffset(13)->cidr());
    EXPECT_EQ(*net_base::IPv4CIDR::CreateFromCIDRString("100.115.92.206/28"),
              container_subnet.AllocateAtOffset(14)->cidr());
  }

  Subnet vm_subnet(
      *net_base::IPv4CIDR::CreateFromAddressAndPrefix(
          ConvertUint32ToIPv4Address(kVmBaseAddress), kVmSubnetPrefixLength),
      base::DoNothing());
  EXPECT_EQ("100.115.92.24/30", vm_subnet.ToCidrString());
  {
    EXPECT_EQ(*net_base::IPv4CIDR::CreateFromCIDRString("100.115.92.25/30"),
              vm_subnet.AllocateAtOffset(1)->cidr());
    EXPECT_EQ(*net_base::IPv4CIDR::CreateFromCIDRString("100.115.92.26/30"),
              vm_subnet.AllocateAtOffset(2)->cidr());
  }

  Subnet parallels_subnet(*net_base::IPv4CIDR::CreateFromAddressAndPrefix(
                              ConvertUint32ToIPv4Address(kParallelsBaseAddress),
                              kParallelsSubnetPrefixLength),
                          base::DoNothing());
  EXPECT_EQ("100.115.92.128/28", parallels_subnet.ToCidrString());
  {
    EXPECT_EQ(*net_base::IPv4CIDR::CreateFromCIDRString("100.115.92.129/28"),
              parallels_subnet.AllocateAtOffset(1)->cidr());
    EXPECT_EQ(*net_base::IPv4CIDR::CreateFromCIDRString("100.115.92.130/28"),
              parallels_subnet.AllocateAtOffset(2)->cidr());
    EXPECT_EQ(*net_base::IPv4CIDR::CreateFromCIDRString("100.115.92.141/28"),
              parallels_subnet.AllocateAtOffset(13)->cidr());
    EXPECT_EQ(*net_base::IPv4CIDR::CreateFromCIDRString("100.115.92.142/28"),
              parallels_subnet.AllocateAtOffset(14)->cidr());
  }
}

// Tests that the Subnet runs the provided cleanup callback when it gets
// destroyed.
TEST(Subnet, Cleanup) {
  bool called = false;

  {
    Subnet subnet(*net_base::IPv4CIDR::CreateFromAddressAndPrefix({}, 24),
                  base::BindOnce(&SetTrue, &called));
  }

  EXPECT_TRUE(called);
}

// Tests that the subnet allows allocating all addresses in the subnet's range
// using an offset.
TEST(ParallelsSubnet, AllocateAtOffset) {
  Subnet subnet(*net_base::IPv4CIDR::CreateFromAddressAndPrefix(
                    ConvertUint32ToIPv4Address(kParallelsBaseAddress),
                    kParallelsSubnetPrefixLength),
                base::DoNothing());

  std::vector<std::unique_ptr<SubnetAddress>> addrs;
  addrs.reserve(subnet.AvailableCount());

  for (uint32_t offset = 1; offset <= subnet.AvailableCount(); ++offset) {
    auto addr = subnet.AllocateAtOffset(offset);
    EXPECT_TRUE(addr);
    EXPECT_EQ(AddOffset(kParallelsBaseAddress, offset),
              addr->cidr().address().ToInAddr().s_addr);
    addrs.emplace_back(std::move(addr));
  }
}

// Tests that the subnet frees addresses when they are destroyed.
TEST(ParallelsSubnet, Free) {
  Subnet subnet(*net_base::IPv4CIDR::CreateFromAddressAndPrefix(
                    ConvertUint32ToIPv4Address(kParallelsBaseAddress),
                    kParallelsSubnetPrefixLength),
                base::DoNothing());

  {
    auto addr = subnet.AllocateAtOffset(1);
    EXPECT_TRUE(addr);
  }

  EXPECT_TRUE(subnet.AllocateAtOffset(1));
}

}  // namespace patchpanel
