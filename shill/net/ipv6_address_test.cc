// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/net/ipv6_address.h"

#include <arpa/inet.h>

#include <array>

#include <base/logging.h>
#include <gtest/gtest.h>

namespace shill {
namespace {
const char kGoodString[] = "fe80::1aa9:5ff:7ebf:14c5";
const IPv6Address::DataType kGoodData = {0xfe, 0x80, 0x00, 0x00, 0x00, 0x00,
                                         0x00, 0x00, 0x1a, 0xa9, 0x05, 0xff,
                                         0x7e, 0xbf, 0x14, 0xc5};

TEST(IPv6AddressTest, DefaultConstructor) {
  const IPv6Address default_addr;
  const IPv6Address::DataType data{0, 0, 0, 0, 0, 0, 0, 0,
                                   0, 0, 0, 0, 0, 0, 0, 0};

  EXPECT_EQ(default_addr.data(), data);
}

TEST(IPv6AddressTest, Constructor) {
  // Constructed from std::array.
  const IPv6Address address1(kGoodData);
  // Constructed from other instance.
  const IPv6Address address2(address1);

  EXPECT_EQ(address1.data(), kGoodData);
  EXPECT_EQ(address1, address2);
}

TEST(IPv6AddressTest, CreateFromString_Success) {
  const auto address = IPv6Address::CreateFromString(kGoodString);
  ASSERT_TRUE(address);
  EXPECT_EQ(address->data(), kGoodData);
}

TEST(IPv6AddressTest, ToString) {
  const IPv6Address address(kGoodData);
  EXPECT_EQ(address.ToString(), kGoodString);
  // Make sure std::ostream operator<<() works.
  LOG(INFO) << "address = " << address;
}

TEST(IPv6AddressTest, CreateFromString_Fail) {
  EXPECT_FALSE(IPv6Address::CreateFromString(""));
  EXPECT_FALSE(IPv6Address::CreateFromString("192.168.10.1"));
}

TEST(IPv6AddressTest, IsZero) {
  const IPv6Address default_addr;
  EXPECT_TRUE(default_addr.IsZero());

  const IPv6Address address(kGoodData);
  EXPECT_FALSE(address.IsZero());
}

TEST(IPv6AddressTest, Order) {
  const IPv6Address kOrderedAddresses[] = {
      *IPv6Address::CreateFromString("::1"),
      *IPv6Address::CreateFromString("2401:fa00:480:c6::30"),
      *IPv6Address::CreateFromString("2401:fa00:480:c6::1:10"),
      *IPv6Address::CreateFromString("2401:fa00:480:f6::6"),
      *IPv6Address::CreateFromString("2401:fa01:480:f6::1"),
      *IPv6Address::CreateFromString("fe80:1000::"),
      *IPv6Address::CreateFromString("ff02::1")};

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
}  // namespace shill
