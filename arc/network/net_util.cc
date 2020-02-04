// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/network/net_util.h"

#include <fstream>
#include <iostream>
#include <random>

#include <base/logging.h>
#include <base/strings/stringprintf.h>

namespace arc_networkd {

std::string IPv4AddressToString(uint32_t addr) {
  char buf[INET_ADDRSTRLEN] = {0};
  struct in_addr ia;
  ia.s_addr = addr;
  return !inet_ntop(AF_INET, &ia, buf, sizeof(buf)) ? "" : buf;
}

std::string IPv4AddressToCidrString(uint32_t addr, uint32_t prefix_length) {
  return IPv4AddressToString(addr) + "/" + std::to_string(prefix_length);
}

std::string MacAddressToString(const MacAddress& addr) {
  return base::StringPrintf("%02x:%02x:%02x:%02x:%02x:%02x", addr[0], addr[1],
                            addr[2], addr[3], addr[4], addr[5]);
}

bool FindFirstIPv6Address(const std::string& ifname, struct in6_addr* address) {
  struct ifaddrs* ifap;
  struct ifaddrs* p;
  bool found = false;

  // Iterate through the linked list of all interface addresses to find
  // the first IPv6 address for |ifname|.
  if (getifaddrs(&ifap) < 0)
    return false;

  for (p = ifap; p; p = p->ifa_next) {
    if (p->ifa_name != ifname || p->ifa_addr->sa_family != AF_INET6) {
      continue;
    }

    if (address) {
      struct sockaddr_in6* sa =
          reinterpret_cast<struct sockaddr_in6*>(p->ifa_addr);
      memcpy(address, &sa->sin6_addr, sizeof(*address));
    }
    found = true;
    break;
  }

  freeifaddrs(ifap);
  return found;
}

bool GenerateRandomIPv6Prefix(struct in6_addr* prefix, int len) {
  std::mt19937 rng;
  rng.seed(std::random_device()());
  std::uniform_int_distribution<std::mt19937::result_type> randbyte(0, 255);

  // TODO(cernekee): handle different prefix lengths
  if (len != 64) {
    LOG(DFATAL) << "Unexpected prefix length";
    return false;
  }

  for (int i = 8; i < 16; i++)
    prefix->s6_addr[i] = randbyte(rng);

  // Set the universal/local flag, similar to a RFC 4941 address.
  prefix->s6_addr[8] |= 0x40;
  return true;
}

std::ostream& operator<<(std::ostream& stream, const struct in_addr& addr) {
  char buf[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &addr, buf, sizeof(buf));
  stream << buf;
  return stream;
}

std::ostream& operator<<(std::ostream& stream, const struct in6_addr& addr) {
  char buf[INET6_ADDRSTRLEN];
  inet_ntop(AF_INET6, &addr, buf, sizeof(buf));
  stream << buf;
  return stream;
}

uint16_t FoldChecksum(uint32_t sum) {
  while (sum >> 16)
    sum = (sum & 0xffff) + (sum >> 16);
  return ~sum;
}

uint32_t NetChecksum(const void* data, ssize_t len) {
  uint32_t sum = 0;
  const uint16_t* word = reinterpret_cast<const uint16_t*>(data);
  for (; len > 1; len -= 2)
    sum += *word++;
  if (len)
    sum += *word & htons(0x0000ffff);
  return sum;
}

uint16_t Ipv4Checksum(const iphdr* ip) {
  uint32_t sum = NetChecksum(ip, sizeof(iphdr));
  return FoldChecksum(sum);
}

uint16_t Udpv4Checksum(const iphdr* ip, const udphdr* udp) {
  uint8_t pseudo_header[12];
  memset(pseudo_header, 0, sizeof(pseudo_header));

  // Fill in the pseudo-header.
  memcpy(pseudo_header, &ip->saddr, sizeof(in_addr));
  memcpy(pseudo_header + 4, &ip->daddr, sizeof(in_addr));
  memcpy(pseudo_header + 9, &ip->protocol, sizeof(uint8_t));
  memcpy(pseudo_header + 10, &udp->len, sizeof(uint16_t));

  // Compute pseudo-header checksum
  uint32_t sum = NetChecksum(pseudo_header, sizeof(pseudo_header));

  // UDP
  sum += NetChecksum(udp, ntohs(udp->len));

  return FoldChecksum(sum);
}

uint16_t Icmpv6Checksum(const ip6_hdr* ip6, const icmp6_hdr* icmp6) {
  uint32_t sum = 0;
  // Src and Dst IP
  for (size_t i = 0; i < (sizeof(struct in6_addr) >> 1); ++i)
    sum += ip6->ip6_src.s6_addr16[i];
  for (size_t i = 0; i < (sizeof(struct in6_addr) >> 1); ++i)
    sum += ip6->ip6_dst.s6_addr16[i];

  // Upper-Layer Packet Length
  sum += ip6->ip6_plen;
  // Next Header
  sum += IPPROTO_ICMPV6 << 8;

  // ICMP
  sum += NetChecksum(icmp6, ntohs(ip6->ip6_plen));

  return FoldChecksum(sum);
}

}  // namespace arc_networkd
