// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net-base/mac_address.h"

#include <gtest/gtest.h>

namespace net_base {
namespace {

TEST(MacAddress, constructor) {
  constexpr MacAddress default_addr;
  EXPECT_TRUE(default_addr.IsZero());
  EXPECT_EQ(default_addr.ToString(), "00:00:00:00:00:00");

  constexpr MacAddress addr1(0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc);
  EXPECT_FALSE(addr1.IsZero());
  EXPECT_EQ(addr1.ToString(), "12:34:56:78:9a:bc");

  constexpr std::array<uint8_t, 6> bytes = {0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc};
  constexpr MacAddress addr2(bytes);
  EXPECT_EQ(addr2, addr1);
}

TEST(MacAddress, CreateFromString) {
  const auto addr = *MacAddress::CreateFromString("12:34:56:78:9a:bc");
  EXPECT_EQ(addr, MacAddress(0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc));
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

}  // namespace
}  // namespace net_base
