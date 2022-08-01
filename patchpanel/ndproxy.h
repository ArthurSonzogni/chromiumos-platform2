// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_NDPROXY_H_
#define PATCHPANEL_NDPROXY_H_

#include <stdint.h>

#include <linux/if_ether.h>
#include <net/ethernet.h>
#include <netinet/icmp6.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <base/files/scoped_file.h>
#include <brillo/daemons/daemon.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#include "patchpanel/mac_address_generator.h"
#include "patchpanel/message_dispatcher.h"

namespace patchpanel {

// Forward ICMPv6 RS/RA/NS/NA mssages between network interfaces according to
// RFC 4389. Support asymmetric proxy that RS will be proxied one-way from
// guest interface to physical interface ('Outbound') and RA the other way back
// ('Inbound'), as well as symmetric proxy among guest interfaces that only
// NS/NA will be proxied.
class NDProxy {
 public:
  static constexpr ssize_t kTranslateErrorNotICMPv6Packet = -1;
  static constexpr ssize_t kTranslateErrorNotNDPacket = -2;
  static constexpr ssize_t kTranslateErrorInsufficientLength = -3;
  static constexpr ssize_t kTranslateErrorBufferMisaligned = -4;
  static constexpr ssize_t kTranslateErrorMismatchedIp6Length = -5;

  NDProxy();
  NDProxy(const NDProxy&) = delete;
  NDProxy& operator=(const NDProxy&) = delete;

  virtual ~NDProxy() = default;

  // RFC 4389: Read the input ICMPv6 packet in |in_packet| and determine whether
  // it should be proxied. If so, fill the |out_packet| buffer with proxied
  // packet and return the length of proxied packet (usually same with input
  // frame length). Return a negative value if proxy is not needed or an error
  // occurred.
  //   in_packet: buffer containing input IPv6 packet.
  //   packet_len: the length of input IPv6 packet;
  //   local_mac_addr: MAC address of interface that will be used to send the
  //       proxied packet;
  //   new_src_ip: if not null, address that will be used for the IP header
  //       source address to send the proxied packet;
  //   new_dst_ip: if not null, address that will be used for the IP header
  //       destination address to send the proxied packet;
  //   out_packet: buffer for output IPv6 pacet; should have at least
  //       packet_len space.
  ssize_t TranslateNDPacket(const uint8_t* in_packet,
                            size_t packet_len,
                            const MacAddress& local_mac_addr,
                            const in6_addr* new_src_ip,
                            const in6_addr* new_dst_ip,
                            uint8_t* out_packet);

  // Given the ICMPv6 packet |icmp6| with header and options (payload) of total
  // byte length |icmp6_len|, returns a pointer to the start of the prefix
  // information, or returns nullptr if no option of type
  // ND_OPT_PREFIX_INFORMATION was found.
  static const nd_opt_prefix_info* GetPrefixInfoOption(const uint8_t* icmp6,
                                                       size_t icmp6_len);

  // Given the ICMPv6 segment |icmp6| with header and options (payload) of total
  // byte length |icmp6_len|, overwrites in option |opt_type| the mac address
  // with |target_mac|. |icmp6_len| is the total size in bytes of the ICMPv6
  // segment. |nd_hdr_len| is the length of ICMPv6 header (so the first option
  // starts after |nd_hdr_len|.)
  static void ReplaceMacInIcmpOption(uint8_t* icmp6,
                                     size_t icmp6_len,
                                     size_t nd_hdr_len,
                                     uint8_t opt_type,
                                     const MacAddress& target_mac);

  // Helper function to create a AF_PACKET socket suitable for frame read/write.
  static base::ScopedFD PreparePacketSocket();

  // Initialize the resources needed such as rtnl socket and dummy socket for
  // ioctl. Return false if failed.
  bool Init();

  // Read one IP packet from AF_PACKET socket |fd| and process it. If proxying
  // is needed, translated packets are sent out through the same socket.
  void ReadAndProcessOnePacket(int fd);

  // NDProxy can trigger a callback upon receiving NA frame with unicast IPv6
  // address from guest OS interface.
  void RegisterOnGuestIpDiscoveryHandler(
      base::RepeatingCallback<void(const std::string&, const std::string&)>
          handler);

  // Callback upon receiving prefix information from RA frame.
  void RegisterOnRouterDiscoveryHandler(
      base::RepeatingCallback<void(int, const std::string&)> handler);

  // Start proxying RS from |if_id_downstream| to |if_id_upstream|, and RA the
  // other way around. If |modify_router_address| is true we modify source
  // address when proxying RA so that downstream thinks ChromeOS host as the
  // router. (b/187918638)
  void StartRSRAProxy(int if_id_upstream,
                      int if_id_downstream,
                      bool modify_router_address = false);
  // Start proxying NS and NA between |if_id1| and |if_id2|.
  void StartNSNAProxy(int if_id1, int if_id2);
  // Stop all proxying between |if_id1| and |if_id2|.
  void StopProxy(int if_id1, int if_id2);

 private:
  // Data structure to store interface mapping for a certain kind of packet to
  // be proxied. For example, {1: {2}, 2: {1}} means that packet from interfaces
  // 1 and 2 will be proxied to each other.
  using interface_mapping = std::map<int, std::set<int>>;

  // Get MAC address on a local interface through ioctl().
  // Returns false upon failure.
  virtual bool GetLocalMac(int if_id, MacAddress* mac_addr);

  // Query kernel NDP table and get the MAC address of a certain IPv6 neighbor.
  // Returns false when neighbor entry is not found.
  virtual bool GetNeighborMac(const in6_addr& ipv6_addr, MacAddress* mac_addr);

  // Get the link local IPv6 address on a local interface.
  // Returns false upon failure.
  virtual bool GetLinkLocalAddress(int if_id, in6_addr* link_local);

  interface_mapping* MapForType(uint8_t type);
  bool IsGuestInterface(int ifindex);
  bool IsRouterInterface(int ifindex);

  // Socket used to communicate with kernel through ioctl. No real packet data
  // goes through this socket.
  base::ScopedFD dummy_fd_;
  base::ScopedFD rtnl_fd_;

  // Fixed buffers for receiving and sending IP packets.
  uint8_t* in_packet_buffer_[IP_MAXPACKET];
  uint8_t* out_packet_buffer_[IP_MAXPACKET];

  // Maps of interface names to set of interfaces to which a given ICMP6 types
  // of ND packet should be forwarded. For any ND packet of a given ICMP6 type
  // arriving on an interface, the relevant map indicates which other interfaces
  // this packets should be proxied to.
  interface_mapping if_map_rs_;
  interface_mapping if_map_ra_;
  interface_mapping if_map_ns_na_;

  // b/187918638: with cellular modems we are observing irregular RAs coming
  // from a src IP that either cannot map to a hardware address in the neighbor
  // table, or is mapped to the local MAC address on the cellular interface.
  // Directly proxying these RAs will cause the guest OS to set up a default
  // route to a next hop that is not reachable for them.
  // For any interface in |irregular_router_ifs_|, a workaround is taken to
  // overwrite the router IP with the host link local IP, so that the guest OS
  // set up the default route with the host as next hop instead.
  std::set<int> irregular_router_ifs_;

  base::RepeatingCallback<void(const std::string&, const std::string&)>
      guest_discovery_handler_;
  base::RepeatingCallback<void(int, const std::string&)>
      router_discovery_handler_;

  base::WeakPtrFactory<NDProxy> weak_factory_{this};

  FRIEND_TEST(NDProxyTest, TranslateFrame);
};

// A wrapper class for running NDProxy in a daemon process. Control messages and
// guest IP discovery messages are passed through |control_fd|.
class NDProxyDaemon : public brillo::Daemon {
 public:
  explicit NDProxyDaemon(base::ScopedFD control_fd);
  NDProxyDaemon(const NDProxyDaemon&) = delete;
  NDProxyDaemon& operator=(const NDProxyDaemon&) = delete;

  virtual ~NDProxyDaemon();

 private:
  // Overrides Daemon init callback. Returns 0 on success and < 0 on error.
  int OnInit() override;
  // FileDescriptorWatcher callbacks for new data on fd_.
  void OnDataSocketReadReady();
  // Callbacks to be registered to msg_dispatcher to handle control messages.
  void OnParentProcessExit();
  void OnControlMessage(const SubprocessMessage& msg);

  // Callback from NDProxy core when receive NA from guest
  void OnGuestIpDiscovery(const std::string& ifname,
                          const std::string& ip6addr);

  // Callback from NDProxy core when receive prefix info from router
  void OnRouterDiscovery(int if_id, const std::string& ip6addr);

  void SendMessage(NDProxyMessage::NDProxyEventType type,
                   const std::string& ifname,
                   const std::string& ip6addr);

  // Map from guest-facing if_index to eui address we assigned
  std::map<int, std::string> guest_if_addrs_;

  // Utilize MessageDispatcher to watch control fd
  std::unique_ptr<MessageDispatcher> msg_dispatcher_;

  // Data fd and its watcher
  base::ScopedFD fd_;
  std::unique_ptr<base::FileDescriptorWatcher::Controller> watcher_;

  NDProxy proxy_;

  base::WeakPtrFactory<NDProxyDaemon> weak_factory_{this};
};

}  // namespace patchpanel

#endif  // PATCHPANEL_NDPROXY_H_
