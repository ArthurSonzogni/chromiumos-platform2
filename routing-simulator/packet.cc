// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "routing-simulator/packet.h"

#include <iostream>
#include <ostream>
#include <string>

#include <base/containers/fixed_flat_map.h>
#include <base/strings/string_util.h>
#include <net-base/ip_address.h>

namespace routing_simulator {
namespace {

std::optional<Packet::Protocol> StrToProtocol(std::string_view protocol) {
  static constexpr auto kStrToProtocol =
      base::MakeFixedFlatMap<std::string_view, Packet::Protocol>({
          {"TCP", Packet::Protocol::kTcp},
          {"UDP", Packet::Protocol::kUdp},
          {"ICMP", Packet::Protocol::kIcmp},
      });
  const auto it = kStrToProtocol.find(base::ToUpperASCII(protocol));
  if (it == kStrToProtocol.end()) {
    return std::nullopt;
  }
  return it->second;
}

// Prompts a user to input protocol information until gets a valid protocol.
// Parse the input string to Packet::Protocol and returns the value.
Packet::Protocol ParseProtocol(std::istream& std_input,
                               std::ostream& std_output) {
  while (true) {
    std::string protocol_str;
    std_output << "Input protocol (TCP/UDP/ICMP): ";
    std_input >> protocol_str;
    const auto result = StrToProtocol(protocol_str);
    if (!result) {
      std_output << "Invalid protocol: " << protocol_str << std::endl;
      continue;
    }
    return *result;
  }
}

// Prompts a user to input destination ip information until gets a valid
// destination ip. Parses the input string to net_base::IPAddress and returns
// the value.
net_base::IPAddress ParseDestinationIp(std::istream& std_input,
                                       std::ostream& std_output) {
  while (true) {
    std::string destination_ip_str;
    std_output << "Input destination ip: ";
    std_input >> destination_ip_str;
    const auto result =
        net_base::IPAddress::CreateFromString(destination_ip_str);
    if (!result) {
      std_output << "Invalid destination ip: " << destination_ip_str
                 << std::endl;
      continue;
    }
    return *result;
  }
}

}  // namespace

// static
// TODO(b/307460180): Add implementations for a user to input |input_interface|,
// |source_ip|, |destination_ip| and |source_port|.
std::optional<Packet> Packet::CreatePacketFromStdin(std::istream& std_input,
                                                    std::ostream& std_output) {
  const auto protocol = ParseProtocol(std_input, std_output);
  const auto destination_ip = ParseDestinationIp(std_input, std_output);
  const auto ip_family = destination_ip.GetFamily();
  return Packet(ip_family, protocol, destination_ip);
}

Packet::Packet(const Packet& other) = default;
Packet& Packet::operator=(const Packet& other) = default;

bool Packet::operator==(const Packet& rhs) const = default;

Packet::Packet(net_base::IPFamily ip_family,
               Protocol protocol,
               net_base::IPAddress destination_ip)
    : ip_family_(ip_family),
      protocol_(protocol),
      source_ip_(ip_family),
      destination_ip_(destination_ip) {}

}  // namespace routing_simulator
