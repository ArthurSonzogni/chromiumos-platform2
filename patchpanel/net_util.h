// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ifaddrs.h>
#include <linux/if_packet.h>
#include <linux/in6.h>
#include <linux/vm_sockets.h>
#include <net/route.h>
#include <netinet/icmp6.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet/udp.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>

#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include <base/strings/stringprintf.h>
#include <base/sys_byteorder.h>
#include <brillo/brillo_export.h>
#include <net-base/ipv4_address.h>

#include "patchpanel/mac_address_generator.h"

#ifndef PATCHPANEL_NET_UTIL_H_
#define PATCHPANEL_NET_UTIL_H_

namespace patchpanel {

// Adds a positive offset of the IPv4Address.
net_base::IPv4Address AddOffset(const net_base::IPv4Address& addr,
                                uint32_t offset);

// Returns a string representation of MAC address given.
BRILLO_EXPORT std::string MacAddressToString(const MacAddress& addr);

BRILLO_EXPORT bool GenerateEUI64Address(in6_addr* address,
                                        const in6_addr& prefix,
                                        const MacAddress& mac);

BRILLO_EXPORT void SetSockaddrIn(struct sockaddr* sockaddr,
                                 const net_base::IPv4Address& addr);

BRILLO_EXPORT std::ostream& operator<<(std::ostream& stream,
                                       const struct in_addr& addr);
BRILLO_EXPORT std::ostream& operator<<(std::ostream& stream,
                                       const struct in6_addr& addr);
BRILLO_EXPORT std::ostream& operator<<(std::ostream& stream,
                                       const struct sockaddr& addr);
BRILLO_EXPORT std::ostream& operator<<(std::ostream& stream,
                                       const struct sockaddr_storage& addr);
BRILLO_EXPORT std::ostream& operator<<(std::ostream& stream,
                                       const struct sockaddr_in& addr);
BRILLO_EXPORT std::ostream& operator<<(std::ostream& stream,
                                       const struct sockaddr_in6& addr);
BRILLO_EXPORT std::ostream& operator<<(std::ostream& stream,
                                       const struct sockaddr_un& addr);
BRILLO_EXPORT std::ostream& operator<<(std::ostream& stream,
                                       const struct sockaddr_vm& addr);
BRILLO_EXPORT std::ostream& operator<<(std::ostream& stream,
                                       const struct sockaddr_ll& addr);

BRILLO_EXPORT std::ostream& operator<<(std::ostream& stream,
                                       const struct rtentry& route);
BRILLO_EXPORT std::ostream& operator<<(std::ostream& stream,
                                       const struct in6_rtmsg& route);

// Fold 32-bit into 16 bits.
BRILLO_EXPORT uint16_t FoldChecksum(uint32_t sum);

// RFC 1071: We are doing calculation directly in network order.
// Note this algorithm works regardless of the endianness of the host.
BRILLO_EXPORT uint32_t NetChecksum(const void* data, size_t len);

BRILLO_EXPORT uint16_t Ipv4Checksum(const iphdr* ip);

// UDPv4 checksum along with IPv4 pseudo-header is defined in RFC 793,
// Section 3.1.
BRILLO_EXPORT uint16_t Udpv4Checksum(const uint8_t* udp_packet, size_t len);

// ICMPv6 checksum is defined in RFC 8200 Section 8.1
BRILLO_EXPORT uint16_t Icmpv6Checksum(const uint8_t* icmp6_packet, size_t len);

// Returns true if multicast forwarding should be enabled for this interface.
BRILLO_EXPORT bool IsMulticastInterface(const std::string& ifname);

}  // namespace patchpanel

#endif  // PATCHPANEL_NET_UTIL_H_
