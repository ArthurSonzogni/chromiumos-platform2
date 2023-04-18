// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/net/ipv4_address.h"

#include <arpa/inet.h>

#include <array>

#include <base/logging.h>
#include <gtest/gtest.h>

namespace shill {
namespace {

TEST(IPv4AddressTest, DefaultConstructor) {
  const IPv4Address default_addr;
  IPv4Address::DataType data{0, 0, 0, 0};

  EXPECT_EQ(default_addr.data(), data);
  EXPECT_EQ(default_addr, IPv4Address(0, 0, 0, 0));
}

TEST(IPv4AddressTest, Constructor) {
  const std::array<uint8_t, 4> data{192, 168, 10, 1};
  // Constructed from raw number.
  const IPv4Address address1(192, 168, 10, 1);
  // Constructed from std::array.
  const IPv4Address address2(data);
  // Constructed from other instance.
  const IPv4Address address3(address1);

  EXPECT_EQ(address1.data(), data);
  EXPECT_EQ(address1, address2);
  EXPECT_EQ(address1, address3);
}

TEST(IPv4AddressTest, CreateFromString_Success) {
  const auto address = IPv4Address::CreateFromString("192.168.10.1");
  ASSERT_TRUE(address);
  EXPECT_EQ(*address, IPv4Address(192, 168, 10, 1));
}

TEST(IPv4AddressTest, ToString) {
  const IPv4Address address(192, 168, 10, 1);
  EXPECT_EQ(address.ToString(), "192.168.10.1");
  // Make sure std::ostream operator<<() works.
  LOG(INFO) << "address = " << address;
}

TEST(IPv4AddressTest, CreateFromString_Fail) {
  EXPECT_FALSE(IPv4Address::CreateFromString(""));
  EXPECT_FALSE(IPv4Address::CreateFromString("192.168.10.1/24"));
  EXPECT_FALSE(IPv4Address::CreateFromString("fe80::1aa9:5ff:7ebf:14c5"));
}

TEST(IPv4AddressTest, IsZero) {
  const IPv4Address default_addr;
  EXPECT_TRUE(default_addr.IsZero());

  const IPv4Address address(0, 0, 0, 1);
  EXPECT_FALSE(address.IsZero());
}

TEST(IPv4AddressTest, Order) {
  const IPv4Address kOrderedAddresses[] = {
      {127, 0, 0, 1},   {192, 168, 1, 1},  {192, 168, 1, 32},
      {192, 168, 2, 1}, {192, 168, 2, 32}, {255, 255, 255, 255}};

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
