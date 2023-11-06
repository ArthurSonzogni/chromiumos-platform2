// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "routing-simulator/packet.h"

#include <iostream>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>

#include <base/containers/fixed_flat_map.h>
#include <base/rand_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
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

// TODO(b/307460180): Support source ip selection by making it optional to input
// source ip when a input interface is not given.
// Prompts a user to input source ip information until gets a valid source ip.
// Parses the input string to net_base::IPAddress and returns the value.
net_base::IPAddress ParseSourceIP(std::istream& std_input,
                                  std::ostream& std_output) {
  while (true) {
    std::string source_ip_str;
    std_output << "Input source ip: ";
    std_input >> source_ip_str;
    const auto result = net_base::IPAddress::CreateFromString(source_ip_str);
    if (!result) {
      std_output << "Invalid source ip: " << source_ip_str << std::endl;
      continue;
    }
    return *result;
  }
}

// TODO(b/307460180):  Verify that dst ip and src ip have the same ip family.
// Prompts a user to input destination ip information until gets a valid
// destination ip. Parses the input string to net_base::IPAddress and returns
// the value.
net_base::IPAddress ParseDestinationIP(std::istream& std_input,
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

// Prompts a user to input destination port information until gets a valid
// destination port. Parses the input string to int and returns the value. If
// the protocol is ICMP, return 0. If input interface is not given when the
// protocol is TCP or UDP, returns randomly generated number as destination
// port.
int ParseDestinationPort(Packet::Protocol protocol,
                         std::istream& std_input,
                         std::ostream& std_output) {
  if (protocol == Packet::Protocol::kIcmp) {
    return 0;
  }
  while (true) {
    std::string destination_port_str;
    std_output << "Input destination port: ";
    std_input >> destination_port_str;
    if (destination_port_str.empty()) {
      int random_port = base::RandInt(1024, 65535);
      std_output
          << "No input. Destination port is set to a randomly generated number "
          << random_port << std::endl;
      return random_port;
    }
    int port = 0;
    if (!base::StringToInt(destination_port_str, &port)) {
      std_output << "Invalid destination port: " << destination_port_str
                 << std::endl;
      continue;
    }
    // Port number is a integer from 1 to 65535.
    if (port < 1 || port > 65535) {
      std_output << "Invalid destination port: " << port << std::endl;
      continue;
    }
    return port;
  }
}

}  // namespace

// static
// TODO(b/307460180): Add implementations for |source_port_| and
// |input_interface|.
// TODO(b/307460180): Add support for the parsing with uid.
std::optional<Packet> Packet::CreatePacketFromStdin(std::istream& std_input,
                                                    std::ostream& std_output) {
  const auto protocol = ParseProtocol(std_input, std_output);
  const auto source_ip = ParseSourceIP(std_input, std_output);
  const auto destination_ip = ParseDestinationIP(std_input, std_output);
  const auto destination_port =
      ParseDestinationPort(protocol, std_input, std_output);
  const auto ip_family = destination_ip.GetFamily();
  return Packet(ip_family, protocol, source_ip, destination_ip,
                destination_port);
}

Packet::Packet(const Packet& other) = default;
Packet& Packet::operator=(const Packet& other) = default;

bool Packet::operator==(const Packet& rhs) const = default;

Packet::Packet(net_base::IPFamily ip_family,
               Protocol protocol,
               net_base::IPAddress source_ip,
               net_base::IPAddress destination_ip,
               int destination_port)
    : ip_family_(ip_family),
      protocol_(protocol),
      source_ip_(source_ip),
      destination_ip_(destination_ip),
      destination_port_(destination_port) {}

}  // namespace routing_simulator
