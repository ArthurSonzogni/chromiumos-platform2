// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net-base/mac_address.h"

#include <set>
#include <unordered_set>

#include <gtest/gtest.h>

namespace net_base {
namespace {

TEST(MacAddress, constructor) {
  constexpr MacAddress default_addr;
  EXPECT_TRUE(default_addr.IsZero());
  EXPECT_EQ(default_addr.ToString(), "00:00:00:00:00:00");
  EXPECT_EQ(default_addr.ToHexString(), "000000000000");

  constexpr MacAddress addr1(0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc);
  EXPECT_FALSE(addr1.IsZero());
  EXPECT_EQ(addr1.ToString(), "12:34:56:78:9a:bc");
  EXPECT_EQ(addr1.ToHexString(), "123456789abc");

  constexpr std::array<uint8_t, 6> bytes = {0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc};
  constexpr MacAddress addr2(bytes);
  EXPECT_EQ(addr2, addr1);
}

TEST(MacAddress, CreateRandom) {
  const std::vector<uint8_t> addr = MacAddress::CreateRandom().ToBytes();
  EXPECT_TRUE(addr[0] & MacAddress::kLocallyAdministratedMacBit);
  EXPECT_FALSE(addr[0] & MacAddress::kMulicastMacBit);
}

TEST(MacAddress, IsLocallyAdministered) {
  MacAddress addr1(0x00, 0x01, 0x02, 0x03, 0x04, 0x05);
  EXPECT_FALSE(addr1.IsLocallyAdministered());

  MacAddress addr2(0x02, 0x01, 0x02, 0x03, 0x04, 0x05);
  EXPECT_TRUE(addr2.IsLocallyAdministered());
}

TEST(MacAddress, CreateFromString) {
  EXPECT_EQ(MacAddress::CreateFromString("12:34:56:78:9a:bc"),
            MacAddress(0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc));
  EXPECT_EQ(MacAddress::CreateFromString("AA:BB:CC:DD:EE:FF"),
            MacAddress(0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff));

  EXPECT_FALSE(MacAddress::CreateFromString("123456789abc").has_value());
}

TEST(MacAddress, CreateFromHexString) {
  EXPECT_EQ(MacAddress::CreateFromHexString("123456789abc"),
            MacAddress(0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc));
  EXPECT_EQ(MacAddress::CreateFromHexString("AABBCCDDEEFF"),
            MacAddress(0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff));

  EXPECT_FALSE(
      MacAddress::CreateFromHexString("12:34:56:78:9a:bC").has_value());
  EXPECT_FALSE(MacAddress::CreateFromHexString("asdf12345678").has_value());
  EXPECT_FALSE(MacAddress::CreateFromHexString("123456789abcef").has_value());
}

TEST(MacAddress, CreateFromBytes) {
  const std::vector<uint8_t> bytes = {0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc};
  const auto addr1 = *MacAddress::CreateFromBytes(bytes);
  EXPECT_EQ(addr1, MacAddress(0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc));
  EXPECT_EQ(addr1.ToBytes(), bytes);

  const char char_bytes[] = {0x12, 0x23, 0x34, 0x45, 0x56, 0x67};
  const auto addr2 = *MacAddress::CreateFromBytes(char_bytes);
  EXPECT_EQ(addr2, MacAddress(0x12, 0x23, 0x34, 0x45, 0x56, 0x67));
}

TEST(MacAddress, CmpOps) {
  const MacAddress kOrderedAddresses[] = {{0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
                                          {0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc},
                                          {0xff, 0xff, 0xff, 0xff, 0xff, 0xff}};

  for (size_t i = 0; i < std::size(kOrderedAddresses); ++i) {
    for (size_t j = 0; j < std::size(kOrderedAddresses); ++j) {
      if (i < j) {
        EXPECT_TRUE(kOrderedAddresses[i] < kOrderedAddresses[j]);
      } else {
        EXPECT_FALSE(kOrderedAddresses[i] < kOrderedAddresses[j]);
      }
    }
  }
}

TEST(MacAddressTest, Container) {
  std::set<MacAddress> set;
  set.insert(MacAddress());

  MacAddress::UnorderedSet unordered_set;
  unordered_set.insert(MacAddress());
}

}  // namespace
}  // namespace net_base
