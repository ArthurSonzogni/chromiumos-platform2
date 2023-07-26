// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net-base/ip_address.h"

#include <base/logging.h>
#include <gtest/gtest.h>

namespace net_base {
namespace {

TEST(IPFamily, ToSAFamily) {
  EXPECT_EQ(ToSAFamily(IPFamily::kIPv4), AF_INET);
  EXPECT_EQ(ToSAFamily(IPFamily::kIPv6), AF_INET6);
}

TEST(IPFamily, FromSAFamily) {
  EXPECT_EQ(FromSAFamily(AF_INET), IPFamily::kIPv4);
  EXPECT_EQ(FromSAFamily(AF_INET6), IPFamily::kIPv6);
  EXPECT_EQ(FromSAFamily(AF_UNSPEC), std::nullopt);
}

TEST(IPFamily, ToString) {
  EXPECT_EQ(ToString(IPFamily::kIPv4), "IPv4");
  EXPECT_EQ(ToString(IPFamily::kIPv6), "IPv6");
}

TEST(IPAddressTest, FamilyConstructor) {
  constexpr IPAddress ipv4_default(IPFamily::kIPv4);
  EXPECT_EQ(ipv4_default.GetFamily(), IPFamily::kIPv4);
  EXPECT_TRUE(ipv4_default.ToIPv4Address()->IsZero());

  constexpr IPAddress ipv6_default(IPFamily::kIPv6);
  EXPECT_EQ(ipv6_default.GetFamily(), IPFamily::kIPv6);
  EXPECT_TRUE(ipv6_default.ToIPv6Address()->IsZero());
}

TEST(IPAddressTest, IPv4Constructor) {
  constexpr IPv4Address ipv4_addr(192, 168, 10, 1);
  constexpr IPAddress address(ipv4_addr);

  EXPECT_EQ(address.GetFamily(), IPFamily::kIPv4);
  EXPECT_EQ(address.ToIPv4Address(), ipv4_addr);
  EXPECT_EQ(address.ToIPv6Address(), std::nullopt);
  EXPECT_EQ(address.ToString(), "192.168.10.1");
  EXPECT_EQ(address.ToByteString(), ipv4_addr.ToByteString());
  EXPECT_EQ(address.ToBytes(), ipv4_addr.ToBytes());
}

TEST(IPAddressTest, IPv6Constructor) {
  constexpr IPv6Address ipv6_addr(0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
                                  0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd,
                                  0xee, 0xff);
  constexpr IPAddress address(ipv6_addr);

  EXPECT_EQ(address.GetFamily(), IPFamily::kIPv6);
  EXPECT_EQ(address.ToIPv4Address(), std::nullopt);
  EXPECT_EQ(address.ToIPv6Address(), ipv6_addr);
  EXPECT_EQ(address.ToString(), "11:2233:4455:6677:8899:aabb:ccdd:eeff");
  EXPECT_EQ(address.ToByteString(), ipv6_addr.ToByteString());
  EXPECT_EQ(address.ToBytes(), ipv6_addr.ToBytes());
}

TEST(IPAddressTest, CreateFromString) {
  const auto ipv4_addr = *IPAddress::CreateFromString("192.168.10.1");
  EXPECT_EQ(ipv4_addr.GetFamily(), IPFamily::kIPv4);
  EXPECT_EQ(ipv4_addr.ToString(), "192.168.10.1");

  const auto ipv6_addr =
      *IPAddress::CreateFromString("11:2233:4455:6677:8899:aabb:ccdd:eeff");
  EXPECT_EQ(ipv6_addr.GetFamily(), IPFamily::kIPv6);
  EXPECT_EQ(ipv6_addr.ToString(), "11:2233:4455:6677:8899:aabb:ccdd:eeff");

  // Bad cases.
  EXPECT_EQ(std::nullopt, IPAddress::CreateFromString(""));
  EXPECT_EQ(std::nullopt, IPAddress::CreateFromString("192.168.10.1/10"));
  EXPECT_EQ(std::nullopt, IPAddress::CreateFromString("::1/10"));
}

TEST(IPAddressTest, CreateFromStringWithFamily) {
  // Correct family.
  EXPECT_NE(std::nullopt, IPAddress::CreateFromString(
                              "192.168.10.1", net_base::IPFamily::kIPv4));
  EXPECT_NE(std::nullopt,
            IPAddress::CreateFromString("::1", net_base::IPFamily::kIPv6));

  // Wrong family.
  EXPECT_EQ(std::nullopt, IPAddress::CreateFromString(
                              "192.168.10.1", net_base::IPFamily::kIPv6));
  EXPECT_EQ(std::nullopt,
            IPAddress::CreateFromString("::1", net_base::IPFamily::kIPv4));
}

TEST(IPAddressTest, CreateFromBytes) {
  constexpr uint8_t ipv4_bytes[4] = {192, 168, 10, 1};
  const std::string ipv4_str = {reinterpret_cast<const char*>(ipv4_bytes),
                                sizeof(ipv4_bytes)};

  const auto ipv4_addr = *IPAddress::CreateFromBytes(ipv4_bytes);
  EXPECT_EQ(ipv4_addr.GetFamily(), IPFamily::kIPv4);
  EXPECT_EQ(ipv4_addr.ToString(), "192.168.10.1");
  EXPECT_EQ(IPAddress::CreateFromBytes(ipv4_str), ipv4_addr);

  constexpr uint8_t ipv6_bytes[16] = {0xfe, 0x80, 0x00, 0x00, 0x00, 0x00,
                                      0x00, 0x00, 0x1a, 0xa9, 0x05, 0xff,
                                      0x7e, 0xbf, 0x14, 0xc5};
  const std::string ipv6_str = {reinterpret_cast<const char*>(ipv6_bytes),
                                sizeof(ipv6_bytes)};
  const auto ipv6_addr = *IPAddress::CreateFromBytes(ipv6_bytes);
  EXPECT_EQ(ipv6_addr.GetFamily(), IPFamily::kIPv6);
  EXPECT_EQ(ipv6_addr.ToString(), "fe80::1aa9:5ff:7ebf:14c5");
  EXPECT_EQ(IPAddress::CreateFromBytes(ipv6_str), ipv6_addr);
}

TEST(IPAddressTest, CreateFromBytesWithFamily) {
  constexpr uint8_t ipv4_bytes[4] = {192, 168, 10, 1};
  constexpr uint8_t ipv6_bytes[16] = {0xfe, 0x80, 0x00, 0x00, 0x00, 0x00,
                                      0x00, 0x00, 0x1a, 0xa9, 0x05, 0xff,
                                      0x7e, 0xbf, 0x14, 0xc5};

  // Correct family.
  EXPECT_NE(std::nullopt, net_base::IPAddress::CreateFromBytes(
                              ipv4_bytes, net_base::IPFamily::kIPv4));
  EXPECT_NE(std::nullopt, net_base::IPAddress::CreateFromBytes(
                              ipv6_bytes, net_base::IPFamily::kIPv6));

  // Wrong family.
  EXPECT_EQ(std::nullopt, net_base::IPAddress::CreateFromBytes(
                              ipv4_bytes, net_base::IPFamily::kIPv6));
  EXPECT_EQ(std::nullopt, net_base::IPAddress::CreateFromBytes(
                              ipv6_bytes, net_base::IPFamily::kIPv4));
}

TEST(IPAddressTest, IsZero) {
  EXPECT_TRUE(IPAddress(IPFamily::kIPv4).IsZero());
  EXPECT_TRUE(IPAddress(IPFamily::kIPv6).IsZero());

  EXPECT_FALSE(IPAddress(IPv4Address(0, 0, 0, 1)).IsZero());
}

TEST(IPAddressTest, OperatorCmp) {
  const IPAddress kOrderedAddresses[] = {
      // We define that a IPv4 address is less than a IPv6 address.
      *IPAddress::CreateFromString("127.0.0.1"),
      *IPAddress::CreateFromString("192.168.1.1"),
      *IPAddress::CreateFromString("192.168.1.32"),
      *IPAddress::CreateFromString("192.168.2.1"),
      *IPAddress::CreateFromString("192.168.2.32"),
      *IPAddress::CreateFromString("255.255.255.255"),
      *IPAddress::CreateFromString("::1"),
      *IPAddress::CreateFromString("2401:fa00:480:c6::30"),
      *IPAddress::CreateFromString("2401:fa00:480:c6::1:10"),
      *IPAddress::CreateFromString("2401:fa00:480:f6::6"),
      *IPAddress::CreateFromString("2401:fa01:480:f6::1"),
      *IPAddress::CreateFromString("fe80:1000::"),
      *IPAddress::CreateFromString("ff02::1")};

  for (size_t i = 0; i < std::size(kOrderedAddresses); ++i) {
    for (size_t j = 0; j < std::size(kOrderedAddresses); ++j) {
      if (i < j) {
        EXPECT_TRUE(kOrderedAddresses[i] < kOrderedAddresses[j]);
        EXPECT_TRUE(kOrderedAddresses[i] != kOrderedAddresses[j]);
      } else {
        EXPECT_FALSE(kOrderedAddresses[i] < kOrderedAddresses[j]);
      }
    }
  }
}

TEST(IPAddressTest, GetAddressLength) {
  EXPECT_EQ(IPAddress(IPFamily::kIPv4).GetAddressLength(), 4);
  EXPECT_EQ(IPAddress(IPFamily::kIPv6).GetAddressLength(), 16);
}

TEST(IPCIDRTest, GetMaxPrefixLength) {
  EXPECT_EQ(IPCIDR::GetMaxPrefixLength(IPFamily::kIPv4), 32);
  EXPECT_EQ(IPCIDR::GetMaxPrefixLength(IPFamily::kIPv6), 128);
}

TEST(IPCIDRTest, CreateFromCIDRString) {
  const auto cidr1 = IPCIDR::CreateFromCIDRString("192.168.10.1/25");
  ASSERT_TRUE(cidr1);
  EXPECT_EQ(cidr1->GetFamily(), IPFamily::kIPv4);
  EXPECT_EQ(cidr1->address(), IPAddress(IPv4Address(192, 168, 10, 1)));
  EXPECT_EQ(cidr1->prefix_length(), 25);

  const auto cidr2 = IPCIDR::CreateFromCIDRString("2401:fa00:480:c6::30/25");
  ASSERT_TRUE(cidr2);
  EXPECT_EQ(cidr2->GetFamily(), IPFamily::kIPv6);
  EXPECT_EQ(cidr2->address(),
            IPAddress(*IPv6Address::CreateFromString("2401:fa00:480:c6::30")));
  EXPECT_EQ(cidr2->prefix_length(), 25);
}

TEST(IPCIDRTest, CreateFromCIDRString_Fail) {
  EXPECT_FALSE(IPCIDR::CreateFromCIDRString("192.168.10.1/-1"));
  EXPECT_FALSE(IPCIDR::CreateFromCIDRString("192.168.10.1/33"));
  EXPECT_FALSE(IPCIDR::CreateFromCIDRString("192.168.10/24"));
  EXPECT_FALSE(IPCIDR::CreateFromCIDRString("2401:fa00:480:c6::30/-1"));
  EXPECT_FALSE(IPCIDR::CreateFromCIDRString("2401:fa00:480:c6::30/130"));
}

TEST(IPCIDRTest, CreateFromCIDRStringWithFamily) {
  // Correct family.
  EXPECT_NE(std::nullopt, IPCIDR::CreateFromCIDRString(
                              "192.168.10.1/25", net_base::IPFamily::kIPv4));
  EXPECT_NE(std::nullopt,
            IPCIDR::CreateFromCIDRString("2401:fa00:480:c6::30/25",
                                         net_base::IPFamily::kIPv6));

  // Wrong family.
  EXPECT_EQ(std::nullopt, IPCIDR::CreateFromCIDRString(
                              "192.168.10.1/25", net_base::IPFamily::kIPv6));
  EXPECT_EQ(std::nullopt,
            IPCIDR::CreateFromCIDRString("2401:fa00:480:c6::30/25",
                                         net_base::IPFamily::kIPv4));
}

TEST(IPCIDRTest, CreateFromStringAndPrefix) {
  const auto cidr1 = IPCIDR::CreateFromStringAndPrefix("192.168.10.1", 25);
  ASSERT_TRUE(cidr1);
  EXPECT_EQ(cidr1->GetFamily(), IPFamily::kIPv4);
  EXPECT_EQ(cidr1->address(), IPAddress(IPv4Address(192, 168, 10, 1)));
  EXPECT_EQ(cidr1->prefix_length(), 25);

  const auto cidr2 = IPCIDR::CreateFromStringAndPrefix("fe80:1000::", 64);
  ASSERT_TRUE(cidr2);
  EXPECT_EQ(cidr2->GetFamily(), IPFamily::kIPv6);
  EXPECT_EQ(cidr2->address(),
            IPAddress(*IPv6Address::CreateFromString("fe80:1000::")));
  EXPECT_EQ(cidr2->prefix_length(), 64);
}

TEST(IPCIDRTest, CreateFromStringAndPrefixWithFamily) {
  // Correct family.
  EXPECT_NE(std::nullopt, IPCIDR::CreateFromStringAndPrefix(
                              "192.168.10.1", 25, net_base::IPFamily::kIPv4));
  EXPECT_NE(std::nullopt, IPCIDR::CreateFromStringAndPrefix(
                              "fe80:1000::", 64, net_base::IPFamily::kIPv6));

  // Wrong family.
  EXPECT_EQ(std::nullopt, IPCIDR::CreateFromStringAndPrefix(
                              "192.168.10.1", 25, net_base::IPFamily::kIPv6));
  EXPECT_EQ(std::nullopt, IPCIDR::CreateFromStringAndPrefix(
                              "fe80:1000::", 64, net_base::IPFamily::kIPv4));
}

TEST(IPCIDRTest, CreateFromBytesAndPrefix) {
  const std::vector<uint8_t> ipv4_bytes{192, 168, 10, 1};
  const std::vector<uint8_t> ipv6_bytes{0xfe, 0x80, 0x00, 0x00, 0x00, 0x00,
                                        0x00, 0x00, 0x1a, 0xa9, 0x05, 0xff,
                                        0x7e, 0xbf, 0x14, 0xc5};
  const std::string ipv4_str = {
      reinterpret_cast<const char*>(ipv4_bytes.data()), ipv4_bytes.size()};
  const std::string ipv6_str = {
      reinterpret_cast<const char*>(ipv6_bytes.data()), ipv6_bytes.size()};

  const auto ipv4_cidr = *IPCIDR::CreateFromBytesAndPrefix(ipv4_bytes, 25);
  EXPECT_EQ(ipv4_cidr.address().ToBytes(), ipv4_bytes);
  EXPECT_EQ(ipv4_cidr.prefix_length(), 25);
  EXPECT_EQ(IPCIDR::CreateFromBytesAndPrefix(ipv4_str, 25), ipv4_cidr);

  const auto ipv6_cidr = *IPCIDR::CreateFromBytesAndPrefix(ipv6_bytes, 64);
  EXPECT_EQ(ipv6_cidr.address().ToBytes(), ipv6_bytes);
  EXPECT_EQ(ipv6_cidr.prefix_length(), 64);
  EXPECT_EQ(IPCIDR::CreateFromBytesAndPrefix(ipv6_str, 64), ipv6_cidr);
}

TEST(IPCIDRTest, CreateFromBytesAndPrefixWithFamily) {
  constexpr uint8_t ipv4_bytes[4] = {192, 168, 10, 1};
  constexpr uint8_t ipv6_bytes[16] = {0xfe, 0x80, 0x00, 0x00, 0x00, 0x00,
                                      0x00, 0x00, 0x1a, 0xa9, 0x05, 0xff,
                                      0x7e, 0xbf, 0x14, 0xc5};

  // Correct family.
  EXPECT_TRUE(IPCIDR::CreateFromBytesAndPrefix(ipv4_bytes, 25,
                                               net_base::IPFamily::kIPv4));
  EXPECT_TRUE(IPCIDR::CreateFromBytesAndPrefix(ipv6_bytes, 64,
                                               net_base::IPFamily::kIPv6));

  // Wrong family.
  EXPECT_FALSE(IPCIDR::CreateFromBytesAndPrefix(ipv4_bytes, 25,
                                                net_base::IPFamily::kIPv6));
  EXPECT_FALSE(IPCIDR::CreateFromBytesAndPrefix(ipv6_bytes, 64,
                                                net_base::IPFamily::kIPv4));
}

TEST(IPCIDRTest, CreateFromAddressAndPrefix) {
  const IPv4Address ipv4_addr(192, 168, 10, 1);
  ASSERT_TRUE(IPCIDR::CreateFromAddressAndPrefix(IPAddress(ipv4_addr), 0));
  ASSERT_TRUE(IPCIDR::CreateFromAddressAndPrefix(IPAddress(ipv4_addr), 25));
  ASSERT_TRUE(IPCIDR::CreateFromAddressAndPrefix(IPAddress(ipv4_addr), 32));

  const auto ipv6_addr = *IPv6Address::CreateFromString("fe80:1000::");
  ASSERT_TRUE(IPCIDR::CreateFromAddressAndPrefix(IPAddress(ipv6_addr), 0));
  ASSERT_TRUE(IPCIDR::CreateFromAddressAndPrefix(IPAddress(ipv6_addr), 64));
  ASSERT_TRUE(IPCIDR::CreateFromAddressAndPrefix(IPAddress(ipv6_addr), 128));
}

TEST(IPCIDRTest, ConstexprConstructor) {
  constexpr IPv4Address ipv4_addr(192, 168, 10, 1);
  constexpr IPv4CIDR ipv4_cidr(ipv4_addr);

  constexpr IPCIDR cidr1(ipv4_addr);
  constexpr IPCIDR cidr2(ipv4_cidr);
  EXPECT_EQ(cidr1.address().ToString(), ipv4_addr.ToString());
  EXPECT_EQ(cidr2.address().ToString(), ipv4_addr.ToString());
}

TEST(IPCIDRTest, FamilyConstructor) {
  constexpr IPCIDR ipv4_default(IPFamily::kIPv4);
  EXPECT_EQ(ipv4_default.GetFamily(), IPFamily::kIPv4);
  EXPECT_EQ(ipv4_default.ToString(), "0.0.0.0/0");

  constexpr IPCIDR ipv6_default(IPFamily::kIPv6);
  EXPECT_EQ(ipv6_default.GetFamily(), IPFamily::kIPv6);
  EXPECT_EQ(ipv6_default.ToString(), "::/0");
}

TEST(IPCIDRTest, GetPrefixAddress) {
  const auto cidr1 = *IPCIDR::CreateFromCIDRString("192.168.10.123/24");
  const auto prefix1 = cidr1.GetPrefixAddress();
  EXPECT_EQ(prefix1.GetFamily(), IPFamily::kIPv4);
  EXPECT_EQ(prefix1.ToString(), "192.168.10.0");

  const auto cidr2 = *IPCIDR::CreateFromCIDRString("2401:fa00:480:f6::6/16");
  const auto prefix2 = cidr2.GetPrefixAddress();
  EXPECT_EQ(prefix2.GetFamily(), IPFamily::kIPv6);
  EXPECT_EQ(prefix2.ToString(), "2401::");
}

TEST(IPCIDRTest, GetBroadcast) {
  const auto cidr1 = *IPCIDR::CreateFromCIDRString("192.168.10.123/24");
  const auto broadcast1 = cidr1.GetBroadcast();
  EXPECT_EQ(broadcast1.GetFamily(), IPFamily::kIPv4);
  EXPECT_EQ(broadcast1.ToString(), "192.168.10.255");
}

TEST(IPCIDRTest, InSameSubnetWith) {
  const auto cidr1 = *IPCIDR::CreateFromCIDRString("192.168.10.123/24");
  EXPECT_TRUE(cidr1.InSameSubnetWith(IPAddress(IPv4Address(192, 168, 10, 1))));
  EXPECT_TRUE(
      cidr1.InSameSubnetWith(IPAddress(IPv4Address(192, 168, 10, 123))));
  EXPECT_TRUE(
      cidr1.InSameSubnetWith(IPAddress(IPv4Address(192, 168, 10, 255))));
  EXPECT_FALSE(
      cidr1.InSameSubnetWith(IPAddress(IPv4Address(192, 168, 11, 123))));
  EXPECT_FALSE(
      cidr1.InSameSubnetWith(IPAddress(IPv4Address(193, 168, 10, 123))));

  const auto cidr2 = *IPCIDR::CreateFromCIDRString("2401:fa00:480:f6::6/16");
  EXPECT_TRUE(cidr2.InSameSubnetWith(
      IPAddress(*IPv6Address::CreateFromString("2401::"))));
  EXPECT_TRUE(cidr2.InSameSubnetWith(
      IPAddress(*IPv6Address::CreateFromString("2401:abc::"))));
  EXPECT_TRUE(cidr2.InSameSubnetWith(
      IPAddress(*IPv6Address::CreateFromString("2401::1"))));
  EXPECT_FALSE(cidr2.InSameSubnetWith(
      IPAddress(*IPv6Address::CreateFromString("2402::6"))));
  EXPECT_FALSE(
      cidr2.InSameSubnetWith(IPAddress(*IPv6Address::CreateFromString("::6"))));
}

TEST(IPCIDRTest, ToString) {
  const std::string cidr_string1 = "192.168.10.123/24";
  const auto cidr1 = *IPCIDR::CreateFromCIDRString(cidr_string1);
  EXPECT_EQ(cidr1.ToString(), cidr_string1);
  // Make sure std::ostream operator<<() works.
  LOG(INFO) << "cidr1 = " << cidr1;

  const std::string cidr_string2 = "2401:fa00:480:c6::1:10/24";
  const auto cidr2 = *IPCIDR::CreateFromCIDRString(cidr_string2);
  EXPECT_EQ(cidr2.ToString(), cidr_string2);
  // Make sure std::ostream operator<<() works.
  LOG(INFO) << "cidr2 = " << cidr2;
}

TEST(IPCIDRTest, ToNetmask) {
  const auto cidr1 = *IPCIDR::CreateFromCIDRString("192.168.2.1/0");
  EXPECT_EQ(cidr1.ToNetmask(), IPAddress(IPv4Address(0, 0, 0, 0)));

  const auto cidr2 = *IPCIDR::CreateFromCIDRString("192.168.2.1/8");
  EXPECT_EQ(cidr2.ToNetmask(), IPAddress(IPv4Address(255, 0, 0, 0)));

  const auto cidr3 = *IPCIDR::CreateFromCIDRString("192.168.2.1/24");
  EXPECT_EQ(cidr3.ToNetmask(), IPAddress(IPv4Address(255, 255, 255, 0)));

  const auto cidr4 = *IPCIDR::CreateFromCIDRString("192.168.2.1/32");
  EXPECT_EQ(cidr4.ToNetmask(), IPAddress(IPv4Address(255, 255, 255, 255)));

  const auto cidr5 = *IPCIDR::CreateFromCIDRString("2401:fa00::1/0");
  EXPECT_EQ(cidr5.ToNetmask(), IPAddress(*IPv6Address::CreateFromString("::")));

  const auto cidr6 = *IPCIDR::CreateFromCIDRString("2401:fa00::1/8");
  EXPECT_EQ(cidr6.ToNetmask(),
            IPAddress(*IPv6Address::CreateFromString("ff00::")));

  const auto cidr7 = *IPCIDR::CreateFromCIDRString("2401:fa00::1/24");
  EXPECT_EQ(cidr7.ToNetmask(),
            IPAddress(*IPv6Address::CreateFromString("ffff:ff00::")));
}

}  // namespace
}  // namespace net_base
