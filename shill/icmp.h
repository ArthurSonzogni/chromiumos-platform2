// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_ICMP_H_
#define SHILL_ICMP_H_

#include <netinet/ip_icmp.h>

#include <memory>

#include <net-base/ip_address.h>
#include <net-base/socket.h>

namespace shill {

class ScopedSocketCloser;
class Sockets;

// The Icmp class encapsulates the task of sending ICMP frames.
class Icmp {
 public:
  static const int kIcmpEchoCode;

  Icmp();
  Icmp(const Icmp&) = delete;
  Icmp& operator=(const Icmp&) = delete;

  virtual ~Icmp();

  // Create a socket for transmission of ICMP frames.
  virtual bool Start(const net_base::IPAddress& destination,
                     int interface_index);

  // Destroy the transmit socket.
  virtual void Stop();

  // Returns whether an ICMP socket is open.
  virtual bool IsStarted() const;

  // Send an ICMP Echo Request (Ping) packet to |destination_|. The ID and
  // sequence number fields of the echo request will be set to |id| and
  // |seq_num| respectively.
  virtual bool TransmitEchoRequest(uint16_t id, uint16_t seq_num);

  int socket() const { return socket_ ? socket_->Get() : -1; }
  const std::optional<net_base::IPAddress>& destination() const {
    return destination_;
  }
  int interface_index() const { return interface_index_; }

 private:
  friend class IcmpSessionTest;
  friend class IcmpTest;

  // IPv4 and IPv6 implementations of TransmitEchoRequest().
  bool TransmitV4EchoRequest(uint16_t id, uint16_t seq_num);
  bool TransmitV6EchoRequest(uint16_t id, uint16_t seq_num);

  // Compute the checksum for Echo Request |hdr| of length |len| according to
  // specifications in RFC 792.
  static uint16_t ComputeIcmpChecksum(const struct icmphdr& hdr, size_t len);

  net_base::Socket::SocketFactory socket_factory_ =
      net_base::Socket::kDefaultSocketFactory;
  std::unique_ptr<net_base::Socket> socket_;

  std::optional<net_base::IPAddress> destination_;
  int interface_index_;
};

}  // namespace shill

#endif  // SHILL_ICMP_H_
