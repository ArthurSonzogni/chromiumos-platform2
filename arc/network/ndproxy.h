// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARC_NETWORK_NDPROXY_H_
#define ARC_NETWORK_NDPROXY_H_

#include <stdint.h>

#include <net/ethernet.h>
#include <netinet/icmp6.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>

#include <map>
#include <memory>
#include <set>
#include <string>

#include <base/files/scoped_file.h>
#include <base/macros.h>
#include <brillo/daemons/daemon.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#include "arc/network/mac_address_generator.h"
#include "arc/network/message_dispatcher.h"

namespace arc_networkd {

// Forward ICMPv6 RS/RA/NS/NA mssages between network interfaces according to
// RFC 4389. Support asymmetric proxy that RS will be proxied one-way from
// guest interface to physical interface ('Outbound') and RA the other way back
// ('Inbound'), as well as symmetric proxy among guest interfaces that only
// NS/NA will be proxied.
class NDProxy {
 public:
  static constexpr ssize_t kTranslateErrorNotICMPv6Frame = -1;
  static constexpr ssize_t kTranslateErrorNotNDFrame = -2;
  static constexpr ssize_t kTranslateErrorInsufficientLength = -3;
  static constexpr ssize_t kTranslateErrorBufferMisaligned = -4;

  NDProxy();
  virtual ~NDProxy() = default;

  ssize_t TranslateNDFrame(const uint8_t* in_frame,
                           ssize_t frame_len,
                           const MacAddress& local_mac_addr,
                           uint8_t* out_frame);

  // Given an extended |buffer|, find a proper frame buffer pointer so that
  // pt > buffer, and start of IP header (pt + ETH_H_LEN) is 4-bytes aligned.
  // In the worst case the size of usable buffer will be original size minus 3.
  // 4x => 4x+2; 4x+1 => 4x+2; 4x+2 => 4x+2; 4x+3 => 4x+6
  static const uint8_t* AlignFrameBuffer(const uint8_t* buffer) {
    return buffer + 3 - (reinterpret_cast<uintptr_t>(buffer + 1) & 0x3);
  }

  static uint8_t* AlignFrameBuffer(uint8_t* buffer) {
    return buffer + 3 - (reinterpret_cast<uintptr_t>(buffer + 1) & 0x3);
  }

  // Initialize the resources needed such as rtnl socket and dummy socket for
  // ioctl. Return false if failed.
  bool Init();

  // Read one frame from AF_PACKET socket |fd| and process it. If proxying is
  // needed, translated frame is sent out through the same socket.
  void ReadAndProcessOneFrame(int fd);

  // NDProxy can trigger a callback upon receiving NA frame with unicast IPv6
  // address from guest OS interface.
  void RegisterOnGuestIpDiscoveryHandler(
      const base::Callback<void(const std::string&, const std::string&)>&
          handler);

  // To proxy between upstream interface and guest OS interface (eth0-arc_eth0)
  // Outbound RS, inbound RA, and bidirectional NS/NA will be proxied.
  bool AddRouterInterfacePair(const std::string& ifname_physical,
                              const std::string& ifname_guest);

  // To proxy between two different guest OS interface (arc_eth0-vmtap0)
  // Only NS/NA will be proxied (bidirectionally).
  bool AddPeeringInterfacePair(const std::string& ifname1,
                               const std::string& ifname2);

  // Remove all proxy interface pair with ifindex.
  bool RemoveInterface(const std::string& ifname);

 private:
  // Data structure to store interface mapping for a certain kind of packet to
  // be proxied. For example, {1: {2}, 2: {1}} means that packet from interfaces
  // 1 and 2 will be proxied to each other.
  using interface_mapping = std::map<int, std::set<int>>;

  static uint16_t Icmpv6Checksum(const ip6_hdr* ip6, const icmp6_hdr* icmp6);
  static void ReplaceMacInIcmpOption(uint8_t* frame,
                                     ssize_t frame_len,
                                     size_t nd_hdr_len,
                                     uint8_t opt_type,
                                     const MacAddress& target_mac);

  // Get MAC address on a local interface through ioctl().
  // Returns false upon failure.
  virtual bool GetLocalMac(int if_id, MacAddress* mac_addr);

  // Query kernel NDP table and get the MAC address of a certain IPv6 neighbor.
  // Returns false when neighbor entry is not found.
  virtual bool GetNeighborMac(const in6_addr& ipv6_addr, MacAddress* mac_addr);

  interface_mapping* MapForType(uint8_t type);
  bool AddInterfacePairInternal(const std::string& ifname1,
                                const std::string& ifname2,
                                bool proxy_rs_ra);
  bool IsGuestInterface(int ifindex);

  // Socket used to communicate with kernel through ioctl. No real packet data
  // goes through this socket.
  base::ScopedFD dummy_fd_;
  base::ScopedFD rtnl_fd_;

  // Allocate slightly more space and adjust the buffer start location to
  // make sure IP header is 4-bytes aligned.
  uint8_t in_frame_buffer_extended_[IP_MAXPACKET + ETH_HLEN + 4];
  uint8_t out_frame_buffer_extended_[IP_MAXPACKET + ETH_HLEN + 4];
  uint8_t* in_frame_buffer_;
  uint8_t* out_frame_buffer_;

  // Utilize MessageDispatcher to watch control fd
  std::unique_ptr<MessageDispatcher> msg_dispatcher_;
  // Data fd and its watcher
  base::ScopedFD fd_;
  std::unique_ptr<base::FileDescriptorWatcher::Controller> watcher_;

  interface_mapping if_map_rs_;
  interface_mapping if_map_ra_;
  interface_mapping if_map_ns_na_;

  base::Callback<void(const std::string&, const std::string&)>
      guest_discovery_handler_;

  base::WeakPtrFactory<NDProxy> weak_factory_{this};

  FRIEND_TEST(NDProxyTest, Icmpv6Checksum);
  FRIEND_TEST(NDProxyTest, TranslateFrame);
  DISALLOW_COPY_AND_ASSIGN(NDProxy);
};

// A wrapper class for running NDProxy in a daemon process. Control messages and
// guest IP discovery messages are passed through |control_fd|.
class NDProxyDaemon : public brillo::Daemon {
 public:
  explicit NDProxyDaemon(base::ScopedFD control_fd);
  virtual ~NDProxyDaemon();

 private:
  // Overrides Daemon init callback. Returns 0 on success and < 0 on error.
  int OnInit() override;
  // FileDescriptorWatcher callbacks for new data on fd_.
  void OnDataSocketReadReady();
  // Callbacks to be registered to msg_dispatcher to handle control messages.
  void OnParentProcessExit();
  void OnDeviceMessage(const DeviceMessage& msg);

  // Callback from NDProxy core when receive NA from guest
  void OnGuestIpDiscovery(const std::string& ifname,
                          const std::string& ip6addr);

  // Utilize MessageDispatcher to watch control fd
  std::unique_ptr<MessageDispatcher> msg_dispatcher_;

  // Data fd and its watcher
  base::ScopedFD fd_;
  std::unique_ptr<base::FileDescriptorWatcher::Controller> watcher_;

  NDProxy proxy_;

  base::WeakPtrFactory<NDProxyDaemon> weak_factory_{this};

  DISALLOW_COPY_AND_ASSIGN(NDProxyDaemon);
};

}  // namespace arc_networkd

#endif  // ARC_NETWORK_NDPROXY_H_
