// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ROUTING_SIMULATOR_PACKET_H_
#define ROUTING_SIMULATOR_PACKET_H_

#include <cstdint>
#include <iostream>
#include <optional>
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
  // - input interface
  // - source ip
  // - destination ip
  // - source port
  // - destination port (if protocol is ICMP, this step will be skipped)
  // TODO(b/307460180): Make it possible to take uid.
  // - uid
  static Packet CreatePacketFromStdin(std::istream& std_input,
                                      std::ostream& std_output);

  // Packet is only copyable.
  Packet(const Packet& other);
  Packet& operator=(const Packet& other);

  // Getter methods for the internal data.
  net_base::IPFamily ip_family() const { return ip_family_; }
  Protocol protocol() const { return protocol_; }
  const net_base::IPAddress& destination_ip() const { return destination_ip_; }
  const net_base::IPAddress& source_ip() const { return source_ip_; }
  int destination_port() const { return destination_port_; }
  int source_port() const { return source_port_; }
  uint32_t fwmark() const { return fwmark_; }
  std::optional<std::string> output_interface() const {
    return output_interface_;
  }
  std::string input_interface() const { return input_interface_; }

  bool operator==(const Packet& rhs) const;

 private:
  Packet(net_base::IPFamily ip_family,
         Protocol protocol,
         const net_base::IPAddress& destination_ip,
         const net_base::IPAddress& source_ip,
         int destination_port,
         int source_port,
         std::string_view input_interface);

  net_base::IPFamily ip_family_;
  Protocol protocol_;
  // TODO(b/307460180): Support source ip selection by setting |source_ip_| to
  // default "0.0.0.0" for IPv4 or "::" for IPv6 when source ip is not given.
  net_base::IPAddress destination_ip_;
  net_base::IPAddress source_ip_;
  // If protocol is ICMP, port number is set to 0, which means it doesn't exist.
  int destination_port_;
  int source_port_;
  uint32_t fwmark_;
  std::string output_interface_;
  std::string input_interface_;
  // TODO(b/307460180): Add uid.
};

}  // namespace routing_simulator

#endif  // ROUTING_SIMULATOR_PACKET_H_
