// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/network/net_util.h"

#include <arpa/inet.h>
#include <byteswap.h>
#include <net/ethernet.h>

#include <gtest/gtest.h>

namespace arc_networkd {

const uint8_t ping_frame[] =
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x86\xdd\x60\x0b"
    "\x8d\xb4\x00\x40\x3a\x40\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x01\x80\x00\xb9\x3c\x13\x8f\x00\x09\xde\x6a"
    "\x78\x5d\x00\x00\x00\x00\x8e\x13\x0f\x00\x00\x00\x00\x00\x10\x11"
    "\x12\x13\x14\x15\x16\x17\x18\x19\x1a\x1b\x1c\x1d\x1e\x1f\x20\x21"
    "\x22\x23\x24\x25\x26\x27\x28\x29\x2a\x2b\x2c\x2d\x2e\x2f\x30\x31"
    "\x32\x33\x34\x35\x36\x37";

const uint8_t rs_frame[] =
    "\x33\x33\x00\x00\x00\x02\x1a\x9b\x82\xbd\xc0\xa0\x86\xdd\x60\x00"
    "\x00\x00\x00\x10\x3a\xff\xfe\x80\x00\x00\x00\x00\x00\x00\x2d\x75"
    "\xb2\x80\x97\x83\x76\xbf\xff\x02\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x02\x85\x00\x2f\xfc\x00\x00\x00\x00\x01\x01"
    "\x1a\x9b\x82\xbd\xc0\xa0";

const uint8_t ip_header[] =
    "\x45\x00\x00\x3d\x7c\x8e\x40\x00\x40\x11\x3d\x36\x64\x73\x5c\x02"
    "\x64\x73\x5c\x03";

const uint8_t udp_packet[] =
    "\x45\x00\x00\x65\x44\xf7\x40\x00\x3f\x11\x7d\x62\x64\x57\x54\x5a"
    "\x64\x73\x5c\x0a\x9d\x6c\x09\xa4\x00\x51\x58\xfb\x70\x72\x6f\x74"
    "\x6f\x63\x6f\x6c\x20\x20\x61\x73\x73\x75\x6d\x65\x73\x20\x20\x74"
    "\x68\x61\x74\x20\x74\x68\x65\x20\x49\x6e\x74\x65\x72\x6e\x65\x74"
    "\x20\x20\x50\x72\x6f\x74\x6f\x63\x6f\x6c\x20\x20\x28\x49\x50\x29"
    "\x20\x20\x5b\x31\x5d\x20\x69\x73\x20\x75\x73\x65\x64\x20\x61\x73"
    "\x20\x74\x68\x65\x0a";

TEST(Byteswap, 16bits) {
  uint32_t test_cases[] = {
      0x0000, 0x0001, 0x1000, 0xffff, 0x2244, 0xfffe,
  };

  for (uint32_t value : test_cases) {
    EXPECT_EQ(Byteswap16(value), bswap_16(value));
    EXPECT_EQ(ntohs(value), Ntohs(value));
    EXPECT_EQ(htons(value), Htons(value));
  }
}

TEST(Byteswap, 32bits) {
  uint32_t test_cases[] = {
      0x00000000, 0x00000001, 0x10000000, 0xffffffff, 0x11335577, 0xdeadbeef,
  };

  for (uint32_t value : test_cases) {
    EXPECT_EQ(Byteswap32(value), bswap_32(value));
    EXPECT_EQ(ntohl(value), Ntohl(value));
    EXPECT_EQ(htonl(value), Htonl(value));
  }
}

TEST(Ipv4, CreationAndStringConversion) {
  struct {
    std::string literal_address;
    uint8_t bytes[4];
  } test_cases[] = {
      {"0.0.0.0", {0, 0, 0, 0}},
      {"8.8.8.8", {8, 8, 8, 8}},
      {"8.8.4.4", {8, 8, 4, 4}},
      {"192.168.0.0", {192, 168, 0, 0}},
      {"100.115.92.5", {100, 115, 92, 5}},
      {"100.115.92.6", {100, 115, 92, 6}},
      {"224.0.0.251", {224, 0, 0, 251}},
      {"255.255.255.255", {255, 255, 255, 255}},
  };

  for (auto const& test_case : test_cases) {
    uint32_t addr = Ipv4Addr(test_case.bytes[0], test_case.bytes[1],
                             test_case.bytes[2], test_case.bytes[3]);
    EXPECT_EQ(test_case.literal_address, IPv4AddressToString(addr));
  }
}

TEST(Ipv4, CreationAndCidrStringConversion) {
  struct {
    std::string literal_address;
    uint8_t bytes[4];
    uint32_t prefix_length;
  } test_cases[] = {
      {"0.0.0.0/0", {0, 0, 0, 0}, 0},
      {"192.168.0.0/24", {192, 168, 0, 0}, 24},
      {"100.115.92.5/30", {100, 115, 92, 5}, 30},
      {"100.115.92.6/30", {100, 115, 92, 6}, 30},
  };

  for (auto const& test_case : test_cases) {
    uint32_t addr = Ipv4Addr(test_case.bytes[0], test_case.bytes[1],
                             test_case.bytes[2], test_case.bytes[3]);
    EXPECT_EQ(test_case.literal_address,
              IPv4AddressToCidrString(addr, test_case.prefix_length));
  }
}

TEST(Ipv4, IpChecksum) {
  alignas(4) uint8_t buffer[IP_MAXPACKET];

  iphdr* ip = reinterpret_cast<iphdr*>(buffer);

  memcpy(buffer, ip_header, sizeof(ip_header));
  uint16_t ori_cksum = ip->check;
  ip->check = 0;
  EXPECT_EQ(ori_cksum, Ipv4Checksum(ip));
}

TEST(Ipv4, UdpChecksum) {
  alignas(4) uint8_t buffer[IP_MAXPACKET];

  iphdr* ip = reinterpret_cast<iphdr*>(buffer);
  udphdr* udp = reinterpret_cast<udphdr*>(buffer + sizeof(iphdr));

  memcpy(buffer, udp_packet, sizeof(udp_packet));
  uint16_t ori_cksum = udp->check;
  udp->check = 0;
  EXPECT_EQ(ori_cksum, Udpv4Checksum(ip, udp));
}

TEST(Ipv6, IcmpChecksum) {
  alignas(4) uint8_t buffer_extended[IP_MAXPACKET + ETHER_HDR_LEN + 2];
  uint8_t* buffer = buffer_extended + 2;

  ip6_hdr* ip6 = reinterpret_cast<ip6_hdr*>(buffer + ETHER_HDR_LEN);
  icmp6_hdr* icmp6 =
      reinterpret_cast<icmp6_hdr*>(buffer + ETHER_HDR_LEN + sizeof(ip6_hdr));

  memcpy(buffer, ping_frame, sizeof(ping_frame));
  uint16_t ori_cksum = icmp6->icmp6_cksum;
  icmp6->icmp6_cksum = 0;
  EXPECT_EQ(ori_cksum, Icmpv6Checksum(ip6, icmp6));

  memcpy(buffer, rs_frame, sizeof(rs_frame));
  ori_cksum = icmp6->icmp6_cksum;
  icmp6->icmp6_cksum = 0;
  EXPECT_EQ(ori_cksum, Icmpv6Checksum(ip6, icmp6));
}

}  // namespace arc_networkd
