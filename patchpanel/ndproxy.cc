// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/ndproxy.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <linux/filter.h>
#include <linux/if_packet.h>
#include <linux/in6.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <netinet/icmp6.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <fstream>
#include <string>
#include <utility>

#include <base/bind.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>

#include "patchpanel/ipc.h"
#include "patchpanel/minijailed_process_runner.h"
#include "patchpanel/net_util.h"

namespace patchpanel {
namespace {
// Currently when we are unable to resolve the destination MAC for a proxied
// packet (note this can only happen for unicast NA and NS), we send the packet
// using all-nodes multicast MAC. Change this flag to true to drop those packets
// on uplinks instead.
// TODO(b/244271776): Investigate if it is safe to drop such packets, or if
// there is a legitimate case that these packets are actually required.
constexpr bool kDropUnresolvableUnicastToUpstream = false;

const unsigned char kZeroMacAddress[] = {0, 0, 0, 0, 0, 0};
const unsigned char kAllNodesMulticastMacAddress[] = {0x33, 0x33, 0,
                                                      0,    0,    0x01};
const unsigned char kAllRoutersMulticastMacAddress[] = {0x33, 0x33, 0,
                                                        0,    0,    0x02};
const unsigned char kSolicitedNodeMulticastMacAddressPrefix[] = {
    0x33, 0x33, 0xff, 0, 0, 0};
const struct in6_addr kAllNodesMulticastAddress = {
    {.u6_addr8 = {0xff, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01}}};
const struct in6_addr kAllRoutersMulticastAddress = {
    {.u6_addr8 = {0xff, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x02}}};
const struct in6_addr kSolicitedNodeMulticastAddressPrefix = {
    {.u6_addr8 = {0xff, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01, 0xff, 0, 0, 0}}};
constexpr int kSolicitedGroupSuffixLength = 3;

// These filter instructions assume that the input is an IPv6 packet and check
// that the packet is an ICMPv6 packet of whose ICMPv6 type is one of: neighbor
// solicitation, neighbor advertisement, router solicitation, or router
// advertisement.
sock_filter kNDPacketBpfInstructions[] = {
    // Load IPv6 next header.
    BPF_STMT(BPF_LD | BPF_B | BPF_IND, offsetof(ip6_hdr, ip6_nxt)),
    // Check if equals ICMPv6, if not, then goto return 0.
    BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, IPPROTO_ICMPV6, 0, 6),
    // Move index to start of ICMPv6 header.
    BPF_STMT(BPF_LDX | BPF_IMM, sizeof(ip6_hdr)),
    // Load ICMPv6 type.
    BPF_STMT(BPF_LD | BPF_B | BPF_IND, offsetof(icmp6_hdr, icmp6_type)),
    // Check if is ND ICMPv6 message.
    BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, ND_ROUTER_SOLICIT, 4, 0),
    BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, ND_ROUTER_ADVERT, 3, 0),
    BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, ND_NEIGHBOR_SOLICIT, 2, 0),
    BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, ND_NEIGHBOR_ADVERT, 1, 0),
    // Return 0.
    BPF_STMT(BPF_RET | BPF_K, 0),
    // Return MAX.
    BPF_STMT(BPF_RET | BPF_K, IP_MAXPACKET),
};
const sock_fprog kNDPacketBpfProgram = {
    .len = sizeof(kNDPacketBpfInstructions) / sizeof(sock_filter),
    .filter = kNDPacketBpfInstructions};

std::string Icmp6TypeName(uint32_t type) {
  switch (type) {
    case ND_ROUTER_SOLICIT:
      return "ND_ROUTER_SOLICIT";
    case ND_ROUTER_ADVERT:
      return "ND_ROUTER_ADVERT";
    case ND_NEIGHBOR_SOLICIT:
      return "ND_NEIGHBOR_SOLICIT";
    case ND_NEIGHBOR_ADVERT:
      return "ND_NEIGHBOR_ADVERT";
    default:
      return "UNKNOWN";
  }
}

[[maybe_unused]] std::string Icmp6ToString(const uint8_t* packet, size_t len) {
  const ip6_hdr* ip6 = reinterpret_cast<const ip6_hdr*>(packet);
  const icmp6_hdr* icmp6 =
      reinterpret_cast<const icmp6_hdr*>(packet + sizeof(ip6_hdr));

  if (len < sizeof(ip6_hdr) + sizeof(icmp6_hdr))
    return "<packet too small>";

  if (ip6->ip6_nxt != IPPROTO_ICMPV6)
    return "<not ICMP6 packet>";

  if (icmp6->icmp6_type < ND_ROUTER_SOLICIT ||
      icmp6->icmp6_type > ND_NEIGHBOR_ADVERT)
    return "<not ND ICMP6 packet>";

  std::stringstream ss;
  ss << Icmp6TypeName(icmp6->icmp6_type) << " "
     << IPv6AddressToString(ip6->ip6_src) << " -> "
     << IPv6AddressToString(ip6->ip6_dst);
  switch (icmp6->icmp6_type) {
    case ND_NEIGHBOR_SOLICIT:
    case ND_NEIGHBOR_ADVERT: {
      // NS and NA has same packet format for Target Address
      ss << ", target "
         << IPv6AddressToString(
                reinterpret_cast<const nd_neighbor_solicit*>(icmp6)
                    ->nd_ns_target);
      break;
    }
    case ND_ROUTER_SOLICIT:
      // Nothing extra to print here
      break;
    case ND_ROUTER_ADVERT: {
      const nd_opt_prefix_info* prefix_info = NDProxy::GetPrefixInfoOption(
          reinterpret_cast<const uint8_t*>(icmp6), len - sizeof(ip6_hdr));
      if (prefix_info != nullptr) {
        ss << ", prefix " << IPv6AddressToString(prefix_info->nd_opt_pi_prefix)
           << "/" << static_cast<int>(prefix_info->nd_opt_pi_prefix_len);
      }
      break;
    }
    default: {
      NOTREACHED();
    }
  }
  return ss.str();
}

}  // namespace

NDProxy::NDProxy() {}

// static
base::ScopedFD NDProxy::PreparePacketSocket() {
  base::ScopedFD fd(
      socket(AF_PACKET, SOCK_DGRAM | SOCK_CLOEXEC, htons(ETH_P_IPV6)));
  if (!fd.is_valid()) {
    PLOG(ERROR) << "socket() failed";
    return base::ScopedFD();
  }
  if (setsockopt(fd.get(), SOL_SOCKET, SO_ATTACH_FILTER, &kNDPacketBpfProgram,
                 sizeof(kNDPacketBpfProgram))) {
    PLOG(ERROR) << "setsockopt(SO_ATTACH_FILTER) failed";
    return base::ScopedFD();
  }
  return fd;
}

bool NDProxy::Init() {
  rtnl_fd_ = base::ScopedFD(
      socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE));
  if (!rtnl_fd_.is_valid()) {
    PLOG(ERROR) << "socket() failed for rtnetlink socket";
    return false;
  }
  sockaddr_nl local = {
      .nl_family = AF_NETLINK,
      .nl_groups = 0,
  };
  if (bind(rtnl_fd_.get(), reinterpret_cast<sockaddr*>(&local), sizeof(local)) <
      0) {
    PLOG(ERROR) << "bind() failed on rtnetlink socket";
    return false;
  }

  dummy_fd_ = base::ScopedFD(socket(AF_INET6, SOCK_DGRAM, 0));
  if (!dummy_fd_.is_valid()) {
    PLOG(ERROR) << "socket() failed for dummy socket";
    return false;
  }
  return true;
}

// static
void NDProxy::ReplaceMacInIcmpOption(uint8_t* icmp6,
                                     size_t icmp6_len,
                                     size_t nd_hdr_len,
                                     uint8_t opt_type,
                                     const MacAddress& target_mac) {
  size_t opt_offset = nd_hdr_len;
  while (opt_offset + sizeof(nd_opt_hdr) <= icmp6_len) {
    nd_opt_hdr* opt = reinterpret_cast<nd_opt_hdr*>(icmp6 + opt_offset);
    // nd_opt_len is in 8 bytes unit.
    size_t opt_len = 8 * (opt->nd_opt_len);
    if (opt_len == 0 || icmp6_len < opt_offset + opt_len) {
      // Invalid packet.
      return;
    }
    if (opt->nd_opt_type == opt_type) {
      if (opt_len < sizeof(nd_opt_hdr) + ETHER_ADDR_LEN) {
        // Option length was inconsistent with the size of a MAC address.
        return;
      }
      memcpy(icmp6 + opt_offset + sizeof(nd_opt_hdr), target_mac.data(),
             ETHER_ADDR_LEN);
    }
    opt_offset += opt_len;
  }
}

// static
ssize_t NDProxy::TranslateNDPacket(const uint8_t* in_packet,
                                   size_t packet_len,
                                   const MacAddress& local_mac_addr,
                                   const in6_addr* new_src_ip,
                                   const in6_addr* new_dst_ip,
                                   uint8_t* out_packet) {
  if (packet_len < sizeof(ip6_hdr) + sizeof(icmp6_hdr)) {
    return kTranslateErrorInsufficientLength;
  }
  if (reinterpret_cast<const ip6_hdr*>(in_packet)->ip6_nxt != IPPROTO_ICMPV6) {
    return kTranslateErrorNotICMPv6Packet;
  }
  if (ntohs(reinterpret_cast<const ip6_hdr*>(in_packet)->ip6_plen) !=
      (packet_len - sizeof(struct ip6_hdr))) {
    return kTranslateErrorMismatchedIp6Length;
  }

  memcpy(out_packet, in_packet, packet_len);
  ip6_hdr* ip6 = reinterpret_cast<ip6_hdr*>(out_packet);
  icmp6_hdr* icmp6 = reinterpret_cast<icmp6_hdr*>(out_packet + sizeof(ip6_hdr));
  const size_t icmp6_len = packet_len - sizeof(ip6_hdr);

  switch (icmp6->icmp6_type) {
    case ND_ROUTER_SOLICIT:
      ReplaceMacInIcmpOption(reinterpret_cast<uint8_t*>(icmp6), icmp6_len,
                             sizeof(nd_router_solicit), ND_OPT_SOURCE_LINKADDR,
                             local_mac_addr);
      break;
    case ND_ROUTER_ADVERT: {
      // RFC 4389 Section 4.1.3.3 - Set Proxy bit
      nd_router_advert* ra = reinterpret_cast<nd_router_advert*>(icmp6);
      if (ra->nd_ra_flags_reserved & 0x04) {
        // According to RFC 4389, an RA packet with 'Proxy' bit set already
        // should not be proxied again, in order to avoid loop. However, we'll
        // need this form of proxy cascading in Crostini (Host->VM->Container)
        // so we are ignoring the check here. Note that we know we are doing RA
        // proxy in only one direction so there should be no loop.
      }
      ra->nd_ra_flags_reserved |= 0x04;

      ReplaceMacInIcmpOption(reinterpret_cast<uint8_t*>(icmp6), icmp6_len,
                             sizeof(nd_router_advert), ND_OPT_SOURCE_LINKADDR,
                             local_mac_addr);
      break;
    }
    case ND_NEIGHBOR_SOLICIT:
      ReplaceMacInIcmpOption(reinterpret_cast<uint8_t*>(icmp6), icmp6_len,
                             sizeof(nd_neighbor_solicit),
                             ND_OPT_SOURCE_LINKADDR, local_mac_addr);
      break;
    case ND_NEIGHBOR_ADVERT:
      ReplaceMacInIcmpOption(reinterpret_cast<uint8_t*>(icmp6), icmp6_len,
                             sizeof(nd_neighbor_advert), ND_OPT_TARGET_LINKADDR,
                             local_mac_addr);
      break;
    default:
      return kTranslateErrorNotNDPacket;
  }

  if (new_src_ip != nullptr) {
    memcpy(&ip6->ip6_src, new_src_ip, sizeof(in6_addr));

    // Turn off onlink flag if we are pretending to be the router.
    nd_opt_prefix_info* prefix_info =
        GetPrefixInfoOption(reinterpret_cast<uint8_t*>(icmp6), icmp6_len);
    if (prefix_info) {
      prefix_info->nd_opt_pi_flags_reserved &= ~ND_OPT_PI_FLAG_ONLINK;
    }
  }
  if (new_dst_ip != nullptr) {
    memcpy(&ip6->ip6_dst, new_dst_ip, sizeof(in6_addr));
  }

  // Recalculate the checksum. We need to clear the old checksum first so
  // checksum calculation does not wrongly take old checksum into account.
  icmp6->icmp6_cksum = 0;
  icmp6->icmp6_cksum =
      Icmpv6Checksum(reinterpret_cast<const uint8_t*>(ip6), packet_len);

  return static_cast<ssize_t>(packet_len);
}

void NDProxy::ReadAndProcessOnePacket(int fd) {
  uint8_t* in_packet = reinterpret_cast<uint8_t*>(in_packet_buffer_);
  uint8_t* out_packet = reinterpret_cast<uint8_t*>(out_packet_buffer_);

  sockaddr_ll recv_ll_addr;
  struct iovec iov_in = {
      .iov_base = in_packet,
      .iov_len = IP_MAXPACKET,
  };
  msghdr hdr = {
      .msg_name = &recv_ll_addr,
      .msg_namelen = sizeof(recv_ll_addr),
      .msg_iov = &iov_in,
      .msg_iovlen = 1,
      .msg_control = nullptr,
      .msg_controllen = 0,
      .msg_flags = 0,
  };

  ssize_t slen;
  if ((slen = recvmsg(fd, &hdr, 0)) < 0) {
    // Ignore ENETDOWN: this can happen if the interface is not yet configured
    if (errno != ENETDOWN) {
      PLOG(WARNING) << "recvmsg() failed";
    }
    return;
  }
  size_t len = static_cast<size_t>(slen);

  ip6_hdr* ip6 = reinterpret_cast<ip6_hdr*>(in_packet);
  icmp6_hdr* icmp6 = reinterpret_cast<icmp6_hdr*>(in_packet + sizeof(ip6_hdr));

  if (ip6->ip6_nxt != IPPROTO_ICMPV6 || icmp6->icmp6_type < ND_ROUTER_SOLICIT ||
      icmp6->icmp6_type > ND_NEIGHBOR_ADVERT)
    return;

  VLOG_IF(2, (icmp6->icmp6_type == ND_ROUTER_SOLICIT ||
              icmp6->icmp6_type == ND_ROUTER_ADVERT))
      << "Received on interface " << recv_ll_addr.sll_ifindex << ": "
      << Icmp6ToString(in_packet, len);
  VLOG_IF(6, (icmp6->icmp6_type == ND_NEIGHBOR_SOLICIT ||
              icmp6->icmp6_type == ND_NEIGHBOR_ADVERT))
      << "Received on interface " << recv_ll_addr.sll_ifindex << ": "
      << Icmp6ToString(in_packet, len);

  NotifyPacketCallbacks(recv_ll_addr.sll_ifindex, in_packet, len);

  if (downlink_link_local_.find(recv_ll_addr.sll_ifindex) !=
          downlink_link_local_.end() &&
      memcmp(&ip6->ip6_dst, &downlink_link_local_[recv_ll_addr.sll_ifindex],
             sizeof(in6_addr)) == 0) {
    // If destination IP is our link local unicast, no need to proxy the packet.
    return;
  }

  // Translate the NDP frame and send it through proxy interface
  auto map_entry =
      MapForType(icmp6->icmp6_type)->find(recv_ll_addr.sll_ifindex);
  if (map_entry == MapForType(icmp6->icmp6_type)->end())
    return;

  const auto& target_ifs = map_entry->second;
  for (int target_if : target_ifs) {
    MacAddress local_mac;
    if (!GetLocalMac(target_if, &local_mac))
      continue;

    // b/246444885: Overwrite source IP address with host address and set
    // prefix offlink, to prevent internal traffic causing ICMP messaged being
    // sent to upstream caused by internal traffic.
    // b/187918638: On L850 only this is a must instead of an optimization.
    // With those modems we are observing irregular RAs coming from a src IP
    // that either cannot map to a hardware address in the neighbor table, or
    // is mapped to the local MAC address on the cellular interface. Directly
    // proxying these RAs will cause the guest OS to set up a default route to
    // a next hop that is not reachable for them.
    const in6_addr* new_src_ip_p = nullptr;
    in6_addr new_src_ip;
    if (modify_ra_uplinks_.find(recv_ll_addr.sll_ifindex) !=
            modify_ra_uplinks_.end() &&
        icmp6->icmp6_type == ND_ROUTER_ADVERT) {
      if (downlink_link_local_.find(target_if) == downlink_link_local_.end()) {
        continue;
      }
      memcpy(&new_src_ip, &downlink_link_local_[target_if], sizeof(in6_addr));
      new_src_ip_p = &new_src_ip;
    }

    // Always proxy RA to multicast address, so that every guest will accept it
    // therefore saving the total amount of RSs we sent to the network.
    // b/228574659: On L850 only this is a must instead of an optimization.
    const in6_addr* new_dst_ip_p = nullptr;
    if (icmp6->icmp6_type == ND_ROUTER_ADVERT) {
      new_dst_ip_p = &kAllNodesMulticastAddress;
    }

    ssize_t result = TranslateNDPacket(in_packet, len, local_mac, new_src_ip_p,
                                       new_dst_ip_p, out_packet);
    if (result < 0) {
      switch (result) {
        case kTranslateErrorNotICMPv6Packet:
          LOG(DFATAL) << "Attempt to TranslateNDPacket on a non-ICMPv6 packet";
          return;
        case kTranslateErrorNotNDPacket:
          LOG(DFATAL) << "Attempt to TranslateNDPacket on a non-NDP packet, "
                         "icmpv6 type = "
                      << static_cast<int>(icmp6->icmp6_type);
          return;
        case kTranslateErrorInsufficientLength:
          LOG(DFATAL) << "TranslateNDPacket failed: packet length = " << len
                      << " is too small";
          return;
        case kTranslateErrorMismatchedIp6Length:
          LOG(DFATAL) << "TranslateNDPacket failed: expected ip6_plen = "
                      << ntohs(ip6->ip6_plen) << ", received length = "
                      << (len - sizeof(struct ip6_hdr));
          return;
        default:
          LOG(DFATAL) << "Unknown error in TranslateNDPacket";
          return;
      }
    }

    sockaddr_ll send_ll_addr = {
        .sll_family = AF_PACKET,
        .sll_protocol = htons(ETH_P_IPV6),
        .sll_ifindex = target_if,
        .sll_halen = ETHER_ADDR_LEN,
    };

    ip6_hdr* new_ip6 = reinterpret_cast<ip6_hdr*>(out_packet);
    ResolveDestinationMac(new_ip6->ip6_dst, send_ll_addr.sll_addr);
    if (memcmp(send_ll_addr.sll_addr, &kZeroMacAddress, ETHER_ADDR_LEN) == 0) {
      VLOG(1) << "Cannot resolve " << Icmp6TypeName(icmp6->icmp6_type)
              << " packet dest IP " << IPv6AddressToString(new_ip6->ip6_dst)
              << " into MAC address. In: " << recv_ll_addr.sll_ifindex
              << ", out: " << target_if;
      if (IsGuestInterface(target_if) || !kDropUnresolvableUnicastToUpstream) {
        // If we can't resolve the destination IP into MAC from kernel neighbor
        // table, fill destination MAC with all-nodes multicast MAC instead.
        memcpy(send_ll_addr.sll_addr, &kAllNodesMulticastMacAddress,
               ETHER_ADDR_LEN);
      } else {
        // Drop the packet.
        return;
      }
    }

    VLOG_IF(3, (icmp6->icmp6_type == ND_ROUTER_SOLICIT ||
                icmp6->icmp6_type == ND_ROUTER_ADVERT))
        << "Sending to interface " << target_if << ": "
        << Icmp6ToString(out_packet, len);
    VLOG_IF(7, (icmp6->icmp6_type == ND_NEIGHBOR_SOLICIT ||
                icmp6->icmp6_type == ND_NEIGHBOR_ADVERT))
        << "Sending to interface " << target_if << ": "
        << Icmp6ToString(out_packet, len);

    struct iovec iov_out = {
        .iov_base = out_packet,
        .iov_len = static_cast<size_t>(len),
    };
    msghdr hdr = {
        .msg_name = &send_ll_addr,
        .msg_namelen = sizeof(send_ll_addr),
        .msg_iov = &iov_out,
        .msg_iovlen = 1,
        .msg_control = nullptr,
        .msg_controllen = 0,
    };
    if (sendmsg(fd, &hdr, 0) < 0) {
      // Ignore ENETDOWN: this can happen if the interface is not yet configured
      if (if_map_ra_.find(target_if) != if_map_ra_.end() && errno != ENETDOWN) {
        PLOG(WARNING) << "sendmsg() failed on interface " << target_if;
      }
    }
  }
}

// static
nd_opt_prefix_info* NDProxy::GetPrefixInfoOption(uint8_t* icmp6,
                                                 size_t icmp6_len) {
  uint8_t* start = reinterpret_cast<uint8_t*>(icmp6);
  uint8_t* end = start + icmp6_len;
  uint8_t* ptr = start + sizeof(nd_router_advert);
  while (ptr + offsetof(nd_opt_hdr, nd_opt_len) < end) {
    nd_opt_hdr* opt = reinterpret_cast<nd_opt_hdr*>(ptr);
    if (opt->nd_opt_len == 0)
      return nullptr;
    ptr += opt->nd_opt_len << 3;  // nd_opt_len is in 8 bytes
    if (ptr > end)
      return nullptr;
    if (opt->nd_opt_type == ND_OPT_PREFIX_INFORMATION &&
        opt->nd_opt_len << 3 == sizeof(nd_opt_prefix_info)) {
      return reinterpret_cast<nd_opt_prefix_info*>(opt);
    }
  }
  return nullptr;
}

// static
const nd_opt_prefix_info* NDProxy::GetPrefixInfoOption(const uint8_t* icmp6,
                                                       size_t icmp6_len) {
  return NDProxy::GetPrefixInfoOption(const_cast<uint8_t*>(icmp6), icmp6_len);
}

void NDProxy::NotifyPacketCallbacks(int recv_ifindex,
                                    const uint8_t* packet,
                                    size_t len) {
  const ip6_hdr* ip6 = reinterpret_cast<const ip6_hdr*>(packet);
  const icmp6_hdr* icmp6 =
      reinterpret_cast<const icmp6_hdr*>(packet + sizeof(ip6_hdr));

  // GuestDiscovery event is triggered whenever an NA advertising global
  // address or an NS with a global source address is received on a downlink.
  if (IsGuestInterface(recv_ifindex) && !guest_discovery_handler_.is_null()) {
    const in6_addr* guest_address = nullptr;
    if (icmp6->icmp6_type == ND_NEIGHBOR_ADVERT) {
      const nd_neighbor_advert* na =
          reinterpret_cast<const nd_neighbor_advert*>(icmp6);
      guest_address = &(na->nd_na_target);
    } else if (icmp6->icmp6_type == ND_NEIGHBOR_SOLICIT) {
      guest_address = &(ip6->ip6_src);
    }
    if (guest_address &&
        ((guest_address->s6_addr[0] & 0xe0) == 0x20 ||   // Global Unicast
         (guest_address->s6_addr[0] & 0xfe) == 0xfc)) {  // Unique Local
      guest_discovery_handler_.Run(recv_ifindex, *guest_address);
      VLOG(2) << "GuestDiscovery on interface " << recv_ifindex << ": "
              << IPv6AddressToString(*guest_address);
    }
  }

  // RouterDiscovery event is triggered whenever an RA is received on a uplink.
  if (icmp6->icmp6_type == ND_ROUTER_ADVERT &&
      IsRouterInterface(recv_ifindex) && !router_discovery_handler_.is_null()) {
    const nd_opt_prefix_info* prefix_info = GetPrefixInfoOption(
        reinterpret_cast<const uint8_t*>(icmp6), len - sizeof(ip6_hdr));
    if (prefix_info != nullptr) {
      router_discovery_handler_.Run(recv_ifindex, prefix_info->nd_opt_pi_prefix,
                                    prefix_info->nd_opt_pi_prefix_len);
      VLOG(2) << "RouterDiscovery on interface " << recv_ifindex << ": "
              << IPv6AddressToString(prefix_info->nd_opt_pi_prefix) << "/"
              << prefix_info->nd_opt_pi_prefix_len;
    }
  }
}

void NDProxy::ResolveDestinationMac(const in6_addr& dest_ipv6,
                                    uint8_t* dest_mac) {
  if (memcmp(&dest_ipv6, &kAllNodesMulticastAddress, sizeof(in6_addr)) == 0) {
    memcpy(dest_mac, &kAllNodesMulticastMacAddress, ETHER_ADDR_LEN);
    return;
  }
  if (memcmp(&dest_ipv6, &kAllRoutersMulticastAddress, sizeof(in6_addr)) == 0) {
    memcpy(dest_mac, &kAllRoutersMulticastMacAddress, ETHER_ADDR_LEN);
    return;
  }
  if (memcmp(&dest_ipv6, &kSolicitedNodeMulticastAddressPrefix,
             sizeof(in6_addr) - kSolicitedGroupSuffixLength) == 0) {
    memcpy(dest_mac, &kSolicitedNodeMulticastMacAddressPrefix, ETHER_ADDR_LEN);
    memcpy(dest_mac + ETHER_ADDR_LEN - kSolicitedGroupSuffixLength,
           &dest_ipv6.s6_addr[sizeof(in6_addr) - kSolicitedGroupSuffixLength],
           kSolicitedGroupSuffixLength);
    return;
  }

  MacAddress neighbor_mac;
  if (GetNeighborMac(dest_ipv6, &neighbor_mac)) {
    memcpy(dest_mac, neighbor_mac.data(), ETHER_ADDR_LEN);
    return;
  }

  memcpy(dest_mac, &kZeroMacAddress, ETHER_ADDR_LEN);
}

bool NDProxy::GetLinkLocalAddress(int ifindex, in6_addr* link_local) {
  DCHECK(link_local != nullptr);
  std::ifstream proc_file("/proc/net/if_inet6");
  std::string line;
  while (std::getline(proc_file, line)) {
    // Line format in /proc/net/if_inet6:
    //   address ifindex prefix_len scope flags ifname
    auto tokens = base::SplitString(line, " \t", base::TRIM_WHITESPACE,
                                    base::SPLIT_WANT_NONEMPTY);
    if (tokens[3] != "20") {
      // We are only looking for link local address (scope value == "20")
      continue;
    }
    int line_if_id;
    if (!base::HexStringToInt(tokens[1], &line_if_id) ||
        line_if_id != ifindex) {
      continue;
    }
    std::vector<uint8_t> line_address;
    if (!base::HexStringToBytes(tokens[0], &line_address) ||
        line_address.size() != sizeof(in6_addr)) {
      continue;
    }
    memcpy(link_local, line_address.data(), sizeof(in6_addr));
    return true;
  }
  return false;
}

bool NDProxy::GetLocalMac(int if_id, MacAddress* mac_addr) {
  ifreq ifr = {
      .ifr_ifindex = if_id,
  };
  if (ioctl(dummy_fd_.get(), SIOCGIFNAME, &ifr) < 0) {
    PLOG(ERROR) << "ioctl() failed to get interface name on interface "
                << if_id;
    return false;
  }
  if (ioctl(dummy_fd_.get(), SIOCGIFHWADDR, &ifr) < 0) {
    PLOG(ERROR) << "ioctl() failed to get MAC address on interface " << if_id;
    return false;
  }
  memcpy(mac_addr->data(), ifr.ifr_addr.sa_data, ETHER_ADDR_LEN);
  return true;
}

bool NDProxy::GetNeighborMac(const in6_addr& ipv6_addr, MacAddress* mac_addr) {
  sockaddr_nl kernel = {
      .nl_family = AF_NETLINK,
      .nl_groups = 0,
  };
  struct nl_req {
    nlmsghdr hdr;
    rtgenmsg gen;
  } req = {
      .hdr =
          {
              .nlmsg_len = NLMSG_LENGTH(sizeof(rtgenmsg)),
              .nlmsg_type = RTM_GETNEIGH,
              .nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP,
              .nlmsg_seq = 1,
          },
      .gen =
          {
              .rtgen_family = AF_INET6,
          },
  };
  iovec io_req = {
      .iov_base = &req,
      .iov_len = req.hdr.nlmsg_len,
  };
  msghdr rtnl_req = {
      .msg_name = &kernel,
      .msg_namelen = sizeof(kernel),
      .msg_iov = &io_req,
      .msg_iovlen = 1,
  };
  if (sendmsg(rtnl_fd_.get(), &rtnl_req, 0) < 0) {
    PLOG(ERROR) << "sendmsg() failed on rtnetlink socket";
    return false;
  }

  static constexpr size_t kRtnlReplyBufferSize = 32768;
  char reply_buffer[kRtnlReplyBufferSize];
  iovec io_reply = {
      .iov_base = reply_buffer,
      .iov_len = kRtnlReplyBufferSize,
  };
  msghdr rtnl_reply = {
      .msg_name = &kernel,
      .msg_namelen = sizeof(kernel),
      .msg_iov = &io_reply,
      .msg_iovlen = 1,
  };

  bool any_entry_matched = false;
  bool done = false;
  while (!done) {
    ssize_t len;
    if ((len = recvmsg(rtnl_fd_.get(), &rtnl_reply, 0)) < 0) {
      PLOG(ERROR) << "recvmsg() failed on rtnetlink socket";
      return false;
    }
    for (nlmsghdr* msg_ptr = reinterpret_cast<nlmsghdr*>(reply_buffer);
         NLMSG_OK(msg_ptr, len); msg_ptr = NLMSG_NEXT(msg_ptr, len)) {
      switch (msg_ptr->nlmsg_type) {
        case NLMSG_DONE: {
          done = true;
          break;
        }
        case RTM_NEWNEIGH: {
          // Bitmap - 0x1: Found IP match; 0x2: found MAC address;
          uint8_t current_entry_status = 0x0;
          uint8_t current_mac[ETHER_ADDR_LEN];
          ndmsg* nd_msg = reinterpret_cast<ndmsg*>(NLMSG_DATA(msg_ptr));
          rtattr* rt_attr = reinterpret_cast<rtattr*>(RTM_RTA(nd_msg));
          size_t rt_attr_len = RTM_PAYLOAD(msg_ptr);
          for (; RTA_OK(rt_attr, rt_attr_len);
               rt_attr = RTA_NEXT(rt_attr, rt_attr_len)) {
            if (rt_attr->rta_type == NDA_DST &&
                memcmp(&ipv6_addr, RTA_DATA(rt_attr), sizeof(in6_addr)) == 0) {
              current_entry_status |= 0x1;
            } else if (rt_attr->rta_type == NDA_LLADDR) {
              current_entry_status |= 0x2;
              memcpy(current_mac, RTA_DATA(rt_attr), ETHER_ADDR_LEN);
            }
          }
          if (current_entry_status == 0x3) {
            memcpy(mac_addr->data(), current_mac, ETHER_ADDR_LEN);
            any_entry_matched = true;
          }
          break;
        }
        default: {
          LOG(WARNING) << "received unexpected rtnetlink message type "
                       << msg_ptr->nlmsg_type << ", length "
                       << msg_ptr->nlmsg_len;
          break;
        }
      }
    }
  }
  return any_entry_matched;
}

void NDProxy::RegisterOnGuestIpDiscoveryHandler(
    base::RepeatingCallback<void(int, const in6_addr&)> handler) {
  guest_discovery_handler_ = std::move(handler);
}

void NDProxy::RegisterOnRouterDiscoveryHandler(
    base::RepeatingCallback<void(int, const in6_addr&, int)> handler) {
  router_discovery_handler_ = std::move(handler);
}

NDProxy::interface_mapping* NDProxy::MapForType(uint8_t type) {
  switch (type) {
    case ND_ROUTER_SOLICIT:
      return &if_map_rs_;
    case ND_ROUTER_ADVERT:
      return &if_map_ra_;
    case ND_NEIGHBOR_SOLICIT:
      return &if_map_ns_;
    case ND_NEIGHBOR_ADVERT:
      return &if_map_na_;
    default:
      LOG(DFATAL) << "Attempt to get interface map on illegal icmpv6 type "
                  << static_cast<int>(type);
      return nullptr;
  }
}

void NDProxy::StartRSRAProxy(int if_id_upstream,
                             int if_id_downstream,
                             bool modify_router_address) {
  VLOG(1) << "StartRARSProxy(" << if_id_upstream << ", " << if_id_downstream
          << (modify_router_address ? ", modify_router_address)" : ")");
  if_map_ra_[if_id_upstream].insert(if_id_downstream);
  if_map_rs_[if_id_downstream].insert(if_id_upstream);
  if (modify_router_address) {
    modify_ra_uplinks_.insert(if_id_upstream);
  }
  downlink_link_local_[if_id_downstream] = in6_addr{};
  if (!GetLinkLocalAddress(if_id_downstream,
                           &downlink_link_local_[if_id_downstream])) {
    LOG(WARNING) << "Cannot find a link local address on interface "
                 << if_id_downstream;
  }
}

void NDProxy::StartNSNAProxy(int if_id_na_side, int if_id_ns_side) {
  VLOG(1) << "StartNSNAProxy(" << if_id_na_side << ", " << if_id_ns_side << ")";
  if_map_na_[if_id_na_side].insert(if_id_ns_side);
  if_map_ns_[if_id_ns_side].insert(if_id_na_side);
}

void NDProxy::StopProxy(int if_id1, int if_id2) {
  VLOG(1) << "StopProxy(" << if_id1 << ", " << if_id2 << ")";

  auto remove_pair = [if_id1, if_id2](interface_mapping& mapping) {
    mapping[if_id1].erase(if_id2);
    if (mapping[if_id1].empty()) {
      mapping.erase(if_id1);
    }
    mapping[if_id2].erase(if_id1);
    if (mapping[if_id2].empty()) {
      mapping.erase(if_id2);
    }
  };
  remove_pair(if_map_ra_);
  remove_pair(if_map_rs_);
  remove_pair(if_map_na_);
  remove_pair(if_map_ns_);
  if (!IsRouterInterface(if_id1)) {
    modify_ra_uplinks_.erase(if_id1);
  }
  if (!IsRouterInterface(if_id2)) {
    modify_ra_uplinks_.erase(if_id2);
  }
  downlink_link_local_.erase(if_id1);
  downlink_link_local_.erase(if_id2);
}

bool NDProxy::IsGuestInterface(int ifindex) {
  return if_map_rs_.find(ifindex) != if_map_rs_.end();
}

bool NDProxy::IsRouterInterface(int ifindex) {
  return if_map_ra_.find(ifindex) != if_map_ra_.end();
}

NDProxyDaemon::NDProxyDaemon(base::ScopedFD control_fd)
    : msg_dispatcher_(
          std::make_unique<MessageDispatcher>(std::move(control_fd))) {}

NDProxyDaemon::~NDProxyDaemon() {}

int NDProxyDaemon::OnInit() {
  // Prevent the main process from sending us any signals.
  if (setsid() < 0) {
    PLOG(ERROR) << "Failed to created a new session with setsid: exiting";
    return EX_OSERR;
  }

  EnterChildProcessJail();

  // Register control fd callbacks
  if (msg_dispatcher_) {
    msg_dispatcher_->RegisterFailureHandler(base::BindRepeating(
        &NDProxyDaemon::OnParentProcessExit, weak_factory_.GetWeakPtr()));
    msg_dispatcher_->RegisterMessageHandler(base::BindRepeating(
        &NDProxyDaemon::OnControlMessage, weak_factory_.GetWeakPtr()));
  }

  // Initialize NDProxy and register guest IP discovery callback
  if (!proxy_.Init()) {
    PLOG(ERROR) << "Failed to initialize NDProxy internal state";
    return EX_OSERR;
  }
  proxy_.RegisterOnGuestIpDiscoveryHandler(base::BindRepeating(
      &NDProxyDaemon::OnGuestIpDiscovery, weak_factory_.GetWeakPtr()));
  proxy_.RegisterOnRouterDiscoveryHandler(base::BindRepeating(
      &NDProxyDaemon::OnRouterDiscovery, weak_factory_.GetWeakPtr()));

  // Initialize data fd
  fd_ = NDProxy::PreparePacketSocket();
  if (!fd_.is_valid()) {
    return EX_OSERR;
  }

  // Start watching on data fd
  watcher_ = base::FileDescriptorWatcher::WatchReadable(
      fd_.get(), base::BindRepeating(&NDProxyDaemon::OnDataSocketReadReady,
                                     weak_factory_.GetWeakPtr()));
  LOG(INFO) << "Started watching on packet fd...";

  return Daemon::OnInit();
}

void NDProxyDaemon::OnDataSocketReadReady() {
  proxy_.ReadAndProcessOnePacket(fd_.get());
}

void NDProxyDaemon::OnParentProcessExit() {
  LOG(ERROR) << "Quitting because the parent process died";
  Quit();
}

void NDProxyDaemon::OnControlMessage(const SubprocessMessage& root_msg) {
  if (!root_msg.has_control_message() ||
      !root_msg.control_message().has_ndproxy_control()) {
    LOG(ERROR) << "Unexpected message type";
    return;
  }
  const NDProxyControlMessage& msg =
      root_msg.control_message().ndproxy_control();
  VLOG(4) << "Received NDProxyControlMessage: " << msg.type() << ": "
          << msg.if_id_primary() << "<->" << msg.if_id_secondary();
  switch (msg.type()) {
    case NDProxyControlMessage::START_NS_NA: {
      proxy_.StartNSNAProxy(msg.if_id_primary(), msg.if_id_secondary());
      proxy_.StartNSNAProxy(msg.if_id_secondary(), msg.if_id_primary());
      break;
    }
    case NDProxyControlMessage::START_NS_NA_RS_RA: {
      proxy_.StartNSNAProxy(msg.if_id_primary(), msg.if_id_secondary());
      proxy_.StartNSNAProxy(msg.if_id_secondary(), msg.if_id_primary());
      proxy_.StartRSRAProxy(msg.if_id_primary(), msg.if_id_secondary());
      break;
    }
    case NDProxyControlMessage::START_NS_NA_RS_RA_MODIFYING_ROUTER_ADDRESS: {
      // TODO(taoyl): therotically whe should be able to stop proxying NS from
      // downlink to uplink and NA from uplink to downlink as we set prefix to
      // be not ONLINK. However, Android ignores the ONLINK flag and always add
      // a local subnet route when receiving a prefix [1]. Consider addressing
      // this in Android so we can remove the first line below.
      // [1] LinkProperties::ensureDirectlyConnectedRoutes()
      proxy_.StartNSNAProxy(msg.if_id_primary(), msg.if_id_secondary());
      proxy_.StartNSNAProxy(msg.if_id_secondary(), msg.if_id_primary());
      proxy_.StartRSRAProxy(msg.if_id_primary(), msg.if_id_secondary(), true);
      break;
    }
    case NDProxyControlMessage::STOP_PROXY: {
      proxy_.StopProxy(msg.if_id_primary(), msg.if_id_secondary());
      break;
    }
    case NDProxyControlMessage::UNKNOWN:
    default:
      NOTREACHED();
  }
}

void NDProxyDaemon::OnGuestIpDiscovery(int if_id, const in6_addr& ip6addr) {
  if (!msg_dispatcher_) {
    return;
  }
  NeighborDetectedSignal msg;
  msg.set_if_id(if_id);
  msg.set_ip(&ip6addr, sizeof(in6_addr));
  NDProxySignalMessage nm;
  *nm.mutable_neighbor_detected_signal() = msg;
  FeedbackMessage fm;
  *fm.mutable_ndproxy_signal() = nm;
  SubprocessMessage root_m;
  *root_m.mutable_feedback_message() = fm;
  msg_dispatcher_->SendMessage(root_m);
}

void NDProxyDaemon::OnRouterDiscovery(int if_id,
                                      const in6_addr& prefix_addr,
                                      int prefix_len) {
  if (!msg_dispatcher_) {
    return;
  }
  RouterDetectedSignal msg;
  msg.set_if_id(if_id);
  msg.set_ip(&prefix_addr, sizeof(in6_addr));
  msg.set_prefix_len(prefix_len);
  NDProxySignalMessage nm;
  *nm.mutable_router_detected_signal() = msg;
  FeedbackMessage fm;
  *fm.mutable_ndproxy_signal() = nm;
  SubprocessMessage root_m;
  *root_m.mutable_feedback_message() = fm;
  msg_dispatcher_->SendMessage(root_m);
}

}  // namespace patchpanel
