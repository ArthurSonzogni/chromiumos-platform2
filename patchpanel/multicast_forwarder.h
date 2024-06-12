// Copyright 2016 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_MULTICAST_FORWARDER_H_
#define PATCHPANEL_MULTICAST_FORWARDER_H_

#include <netinet/ip.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>

#include <base/files/scoped_file.h>
#include <chromeos/net-base/ipv4_address.h>
#include <chromeos/net-base/ipv6_address.h>
#include <chromeos/net-base/socket.h>

namespace patchpanel {

constexpr net_base::IPv4Address kMdnsMcastAddress(224, 0, 0, 251);
constexpr net_base::IPv6Address kMdnsMcastAddress6(
    0xff, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xfb);
constexpr uint16_t kMdnsPort = 5353;
constexpr net_base::IPv4Address kSsdpMcastAddress(239, 255, 255, 250);
constexpr net_base::IPv6Address kSsdpMcastAddress6(
    0xff, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xc);
constexpr uint16_t kSsdpPort = 1900;

// Listens on a well-known port and forwards multicast messages between
// network interfaces.  Handles mDNS, legacy mDNS, and SSDP messages.
// MulticastForwarder forwards multicast between 1 physical interface and
// many guest interfaces.
class MulticastForwarder {
  // Indicates direction of multicast forwarding.
 public:
  enum class Direction { kInboundOnly = 0, kOutboundOnly = 1, kTwoWays = 2 };

  MulticastForwarder(std::string_view lan_ifname,
                     const net_base::IPv4Address& mcast_addr,
                     const net_base::IPv6Address& mcast_addr6,
                     uint16_t port);
  MulticastForwarder(const MulticastForwarder&) = delete;
  MulticastForwarder& operator=(const MulticastForwarder&) = delete;

  virtual ~MulticastForwarder() = default;

  // Starts multicast listening on |lan_ifname| for addresses |mcast_addr_| and
  // |mcast_addr6_| on port |port_|.
  void Init();

  // Start forwarding multicast packets between the guest's interface
  // |int_ifname| and the external LAN interface |lan_ifname|.  This
  // only forwards traffic on multicast address |mcast_addr_| or
  // |mcast_addr6_| and UDP port |port|.
  // |dir| indicates the direction of traffic we want to start forwarding.
  bool StartForwarding(std::string_view int_ifname, Direction dir);

  // Stop forwarding multicast packets between |int_ifname| and
  // |lan_ifname_|.
  // |dir| indicates the direction of traffic we want to stop forwarding.
  void StopForwarding(std::string_view int_ifname, Direction dir);

  // Rewrite mDNS A records pointing to |guest_ip| so that they point to
  // the IPv4 |lan_ip| assigned to physical interface instead, so that Android
  // can advertise services to devices on the LAN.  This modifies |data|, an
  // incoming packet that is |len| long.
  static void TranslateMdnsIp(const struct in_addr& lan_ip,
                              const struct in_addr& guest_ip,
                              char* data,
                              size_t len);

  // |int_ifname| is expected to be empty when called on LAN socket.
  void OnFileCanReadWithoutBlocking(int fd,
                                    sa_family_t sa_family,
                                    std::optional<std::string_view> int_ifname);

 protected:
  // SocketWithError is used to keep track of a socket and last errno.
  struct SocketWithError {
    std::unique_ptr<net_base::Socket> socket;

    // Keep track of last errno to avoid spammy logs.
    int last_errno = 0;
  };

  // IntSocket is used to keep track of a internal socket. |inbound| and
  // |outbound| indicates whether inbound traffic or outbound traffic is
  // allowed.
  struct IntSocket {
    SocketWithError sock_with_err;
    bool inbound;
    bool outbound;
  };

  // Creates a multicast socket.
  virtual std::unique_ptr<net_base::Socket> Bind(sa_family_t sa_family,
                                                 std::string_view ifname);

  // SendTo sends |data| using a socket bound to |src_port| and |lan_ifname_|.
  // If |src_port| is equal to |port_|, we will use |lan_socket_|. Otherwise,
  // create a temporary socket.
  virtual bool SendTo(uint16_t src_port,
                      const void* data,
                      size_t len,
                      const struct sockaddr* dst,
                      socklen_t dst_len);

  // SendToGuests will forward packet to all Chrome OS guests' (ARC++,
  // Crostini, etc) internal fd using |port| with two exceptions:
  // 1. if ignore_fd is not 0, it will skip guest with fd = ignore_fd.
  // 2. if inbound flag is set to false, skip for this guest. Currently this
  //    only affects ARC.
  virtual bool SendToGuests(const void* data,
                            size_t len,
                            const struct sockaddr* dst,
                            socklen_t dst_len,
                            int ignore_fd = -1);

  // Wrapper around libc recvfrom, allowing override in fuzzer tests.
  virtual ssize_t Receive(int fd,
                          char* buffer,
                          size_t buffer_size,
                          struct sockaddr* src_addr,
                          socklen_t* addrlen);

  // Create a LAN socket.
  SocketWithError CreateLanSocket(std::unique_ptr<net_base::Socket> socket,
                                  sa_family_t family);

  // Create an internal socket, |inbound| and |outbound| indicates whether
  // inbound or outbound forwarding is allowed.
  IntSocket CreateIntSocket(std::unique_ptr<net_base::Socket> socket,
                            sa_family_t family,
                            std::string_view int_ifname,
                            bool outbound,
                            bool inbound);

 private:
  // Name of the physical interface that this forwarder is bound to.
  std::string lan_ifname_;
  // UDP port of the protocol that this forwarder is processing.
  uint16_t port_;
  // IPv4 multicast address of the protocol that this forwarder is processing.
  net_base::IPv4Address mcast_addr_;
  // IPv6 multicast address of the protocol that this forwarder is processing.
  net_base::IPv6Address mcast_addr6_;
  // IPv4 and IPv6 sockets bound by this forwarder onto |lan_ifname_|.
  std::map<sa_family_t, SocketWithError> lan_socket_;
  // Mapping from pair of internal interface names and IP family to tuple of
  // internal sockets and flags for multicast inbound and outbound traffic.
  std::map<std::pair<sa_family_t, std::string>, IntSocket, std::less<>>
      int_sockets_;
};

}  // namespace patchpanel

#endif  // PATCHPANEL_MULTICAST_FORWARDER_H_
