// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_BROADCAST_FORWARDER_H_
#define PATCHPANEL_BROADCAST_FORWARDER_H_

#include <sys/socket.h>
#include <sys/types.h>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>

#include <chromeos/net-base/ipv4_address.h>
#include <chromeos/net-base/rtnl_listener.h>
#include <chromeos/net-base/rtnl_message.h>
#include <chromeos/net-base/socket.h>

namespace patchpanel {

// Listens to broadcast messages sent by applications and forwards them between
// network interfaces of host and guest.
// BroadcastForwarder assumes that guest addresses, including broadcast and
// netmask, are constant.
class BroadcastForwarder {
 public:
  explicit BroadcastForwarder(std::string_view lan_ifname);
  BroadcastForwarder(const BroadcastForwarder&) = delete;
  BroadcastForwarder& operator=(const BroadcastForwarder&) = delete;

  virtual ~BroadcastForwarder() = default;

  // Starts or stops forwarding broadcast packets to and from a downstream
  // guest on network interface |int_ifname|.
  bool AddGuest(std::string_view int_ifname);
  void RemoveGuest(std::string_view int_ifname);

  // Receives a broadcast packet from the network or from a guest and forwards
  // it.
  void OnFileCanReadWithoutBlocking(int fd);

  // Callback from RTNetlink listener, invoked when the lan interface IPv4
  // address is changed.
  void AddrMsgHandler(const net_base::RTNLMessage& msg);

 protected:
  // SocketWithIPv4Addr keeps track of an socket fd and addresses corresponding
  // to the interface it is bound to.
  struct SocketWithIPv4Addr {
    std::unique_ptr<net_base::Socket> socket;
    net_base::IPv4Address addr;
    net_base::IPv4Address broadaddr;
    net_base::IPv4Address netmask;
  };

  // Creates |dev_socket_| and starts to listen to RTNL IPv4 address events.
  // Returns whether or not the initialization succeeded.
  bool LazyInit();

  // Creates a broadcast socket which is used for sending broadcasts.
  virtual std::unique_ptr<net_base::Socket> Bind(std::string_view ifname,
                                                 uint16_t port);

  // Creates a broadcast socket that listens to all IP packets.
  // It filters the packets to only broadcast packets that is sent by
  // applications.
  // This is used to listen on broadcasts.
  virtual std::unique_ptr<net_base::Socket> BindRaw(std::string_view ifname);

  // SendToNetwork sends |data| using a socket bound to |src_port| and
  // |lan_ifname_| using a temporary socket.
  bool SendToNetwork(uint16_t src_port,
                     const void* data,
                     size_t len,
                     const struct sockaddr_in& dst);

  // SendToGuests will forward the broadcast packet to all Chrome OS guests'
  // (ARC++, Crostini, etc) internal fd.
  bool SendToGuests(const void* ip_pkt,
                    size_t len,
                    const struct sockaddr_in& dst);

  // Wrapper around libc recvmsg, allowing override in fuzzer tests.
  virtual ssize_t ReceiveMessage(int fd, struct msghdr* msg);

  // Wrapper around libc sendto, allowing override in fuzzer tests.
  virtual ssize_t SendTo(int fd,
                         const void* buffer,
                         size_t buffer_len,
                         const struct sockaddr_in* dst_addr);

  virtual std::unique_ptr<SocketWithIPv4Addr> CreateSocket(
      std::unique_ptr<net_base::Socket> socket,
      const net_base::IPv4Address& addr,
      const net_base::IPv4Address& broadaddr,
      const net_base::IPv4Address& netmask);

 private:
  // Listens for RTMGRP_IPV4_IFADDR messages and invokes AddrMsgHandler.
  std::unique_ptr<net_base::RTNLListener> addr_listener_;
  // Name of the physical interface that this forwarder is bound to.
  const std::string lan_ifname_;
  // IPv4 socket bound by this forwarder onto |lan_ifname_|.
  std::unique_ptr<SocketWithIPv4Addr> dev_socket_;
  // Mapping from guest bridge interface name to its sockets.
  std::map<std::string, std::unique_ptr<SocketWithIPv4Addr>, std::less<>>
      br_sockets_;

  base::WeakPtrFactory<BroadcastForwarder> weak_factory_{this};
};

}  // namespace patchpanel

#endif  // PATCHPANEL_BROADCAST_FORWARDER_H_
