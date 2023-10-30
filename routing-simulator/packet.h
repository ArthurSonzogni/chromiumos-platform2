// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ROUTING_SIMULATOR_PACKET_H_
#define ROUTING_SIMULATOR_PACKET_H_

#include <cstdint>
#include <string>

#include <net-base/ip_address.h>

namespace routing_simulator {

// Represents a packet.
class Packet {
 public:
  enum class Protocol {
    kTcp,
    kUdp,
    kIcmp,
  };

  // Creates a Packet object from user inputs. Outputs texts to prompt a user to
  // input each item in a packet with verification and sets it to the
  // corresponding field of the packet object. At present, a user must input
  // valid strings for source ip.
  // Prompts a user to input the following fields:
  // - protocol (TCP, UDP or ICMP)
  // - input interface (if the packet type is egress, it's optional)
  // - source ip (if the input interface is not given, it's optional)
  // - source port (if the input interface is not given, it's optional)
  // - destination ip
  // - destination port
  // TODO(b/307460180): Make source ip optional and make it possible to take
  // user id.
  static std::optional<Packet> CreatePacketFromStdin();

  // Packet is only copyable.
  Packet(const Packet& other);
  Packet& operator=(const Packet& other);

  // Getter methods for the internal data.
  net_base::IPFamily ip_family() const { return ip_family_; }
  Protocol protocol() const { return protocol_; }
  const net_base::IPAddress& source_ip() const { return source_ip_; }
  const net_base::IPAddress& destination_ip() const { return destination_ip_; }
  int source_port() const { return source_port_; }
  int destination_port() const { return destination_port_; }
  uint32_t fwmark() const { return fwmark_; }
  const std::string& output_interface() const { return output_interface_; }
  const std::string& input_interface() const { return input_interface_; }

  bool operator==(const Packet& rhs) const;

 private:
  explicit Packet(net_base::IPFamily ip_family);

  net_base::IPFamily ip_family_;
  Protocol protocol_;
  net_base::IPAddress source_ip_;
  net_base::IPAddress destination_ip_;
  int source_port_;
  int destination_port_;
  uint32_t fwmark_;
  std::string output_interface_;
  std::string input_interface_;
  // TODO(b/307460180): Add uid.
};

}  // namespace routing_simulator

#endif  // ROUTING_SIMULATOR_PACKET_H_
