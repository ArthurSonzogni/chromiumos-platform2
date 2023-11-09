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
#include <base/logging.h>
#include <base/rand_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <net-base/ip_address.h>

namespace routing_simulator {
namespace {

enum class DstOrSrc { kDst, kSrc };

std::ostream& operator<<(std::ostream& stream, const DstOrSrc dst_or_src) {
  switch (dst_or_src) {
    case DstOrSrc::kDst:
      return stream << "destination";
    case DstOrSrc::kSrc:
      return stream << "source";
  }
}

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
    std::getline(std_input, protocol_str);
    protocol_str =
        base::TrimWhitespaceASCII(protocol_str, base::TrimPositions::TRIM_ALL);
    const auto result = StrToProtocol(protocol_str);
    if (!result) {
      std_output << "Invalid protocol: " << protocol_str << std::endl;
      continue;
    }
    return *result;
  }
}

// TODO(b/307460180): Check if the given input interface is on the DUT.
// Prompts a user to input input interface information until gets a valid input
// interface. Parse the input string and returns the value.
std::string ParseInputInterface(std::istream& std_input,
                                std::ostream& std_output) {
  while (true) {
    std::string input_interface;
    std_output << "Input input interface: ";
    std::getline(std_input, input_interface);
    if (input_interface.empty()) {
      std_output << "Input interface is empty, assume it is an egress packet"
                 << std::endl;
      return input_interface;
    }
    const auto result_tokens = base::SplitStringPiece(
        input_interface, " ", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
    if (result_tokens.size() > 1) {
      std_output << "Invalid input interface: it contains whitespaces "
                 << input_interface << std::endl;
      continue;
    }
    if (result_tokens.empty()) {
      std_output << "Invalid input interface: only whitespaces" << std::endl;
      continue;
    }
    return std::string(result_tokens[0]);
  }
}

// Prompts a user to input destination ip information until gets a valid
// destination ip. Parses the input string to net_base::IPAddress and returns
// the value.
net_base::IPAddress ParseDestinationIP(std::istream& std_input,
                                       std::ostream& std_output) {
  while (true) {
    std::string destination_ip_str;
    std_output << "Input destination ip: ";
    std::getline(std_input, destination_ip_str);
    destination_ip_str = base::TrimWhitespaceASCII(
        destination_ip_str, base::TrimPositions::TRIM_ALL);
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

// TODO(b/307460180): Support source ip selection by making it optional to input
// source ip when a input interface is not given.
// Prompts a user to input source ip information until gets a valid source ip.
// Parses the input string to net_base::IPAddress and returns the value.
net_base::IPAddress ParseSourceIP(net_base::IPFamily ip_family,
                                  std::istream& std_input,
                                  std::ostream& std_output) {
  while (true) {
    std::string source_ip_str;
    std_output << "Input source ip: ";
    std::getline(std_input, source_ip_str);
    source_ip_str =
        base::TrimWhitespaceASCII(source_ip_str, base::TrimPositions::TRIM_ALL);
    const auto result = net_base::IPAddress::CreateFromString(source_ip_str);
    if (!result) {
      std_output << "Invalid source ip: " << source_ip_str << std::endl;
      continue;
    }
    if (result->GetFamily() != ip_family) {
      std_output << "Please input source ip in "
                 << net_base::ToString(ip_family) << std::endl;
      continue;
    }
    return *result;
  }
}

// Prompts a user to input port information until gets a valid port. Parses the
// input string to int and returns the value. If the protocol is ICMP, return 0.
// If input interface is not given when the protocol is TCP or UDP, returns
// randomly generated number as port.
int ParsePort(DstOrSrc dst_or_src,
              Packet::Protocol protocol,
              std::istream& std_input,
              std::ostream& std_output) {
  if (protocol == Packet::Protocol::kIcmp) {
    return 0;
  }
  while (true) {
    std::string port_str;
    std_output << "Input " << dst_or_src << " port: ";
    std::getline(std_input, port_str);
    port_str =
        base::TrimWhitespaceASCII(port_str, base::TrimPositions::TRIM_ALL);
    int port = 0;
    if (port_str.empty()) {
      int random_port = base::RandInt(1024, 65535);
      std_output << "No input: " << dst_or_src
                 << " port is set to a randomly generated number "
                 << random_port << std::endl;
      return random_port;
    }
    if (!base::StringToInt(port_str, &port)) {
      std_output << "Invalid " << dst_or_src << " port: " << port_str
                 << std::endl;
      continue;
    }
    // Port number is a integer from 1 to 65535.
    if (port < 1 || port > 65535) {
      std_output << "Invalid " << dst_or_src << " port: " << port
                 << " is not from 1 to 65535" << std::endl;
      continue;
    }
    return port;
  }
}

}  // namespace

// static
// TODO(b/307460180): Add support for the parsing with uid.
Packet Packet::CreatePacketFromStdin(std::istream& std_input,
                                     std::ostream& std_output) {
  const auto protocol = ParseProtocol(std_input, std_output);
  const auto input_interface = ParseInputInterface(std_input, std_output);
  const auto destination_ip = ParseDestinationIP(std_input, std_output);
  const auto ip_family = destination_ip.GetFamily();
  const auto source_ip = ParseSourceIP(ip_family, std_input, std_output);
  const int destination_port =
      ParsePort(DstOrSrc::kDst, protocol, std_input, std_output);
  const int source_port =
      ParsePort(DstOrSrc::kSrc, protocol, std_input, std_output);
  return Packet(ip_family, protocol, destination_ip, source_ip,
                destination_port, source_port, input_interface);
}

// static
std::optional<Packet> Packet::CreatePacketForTesting(
    net_base::IPFamily ip_family,
    Packet::Protocol protocol,
    const net_base::IPAddress& destination_ip,
    const net_base::IPAddress& source_ip,
    int destination_port,
    int source_port,
    std::string_view input_interface) {
  if (ip_family != destination_ip.GetFamily() ||
      ip_family != source_ip.GetFamily()) {
    LOG(ERROR)
        << "Input destination IP or source IP contradicts input IP family";
    return std::nullopt;
  }
  return Packet(ip_family, protocol, destination_ip, source_ip,
                destination_port, source_port, input_interface);
}

Packet::Packet(const Packet& other) = default;
Packet& Packet::operator=(const Packet& other) = default;

bool Packet::operator==(const Packet& rhs) const = default;

Packet::Packet(net_base::IPFamily ip_family,
               Protocol protocol,
               const net_base::IPAddress& destination_ip,
               const net_base::IPAddress& source_ip,
               int destination_port,
               int source_port,
               std::string_view input_interface)
    : ip_family_(ip_family),
      protocol_(protocol),
      destination_ip_(destination_ip),
      source_ip_(source_ip),
      destination_port_(destination_port),
      source_port_(source_port),
      input_interface_(input_interface) {}

}  // namespace routing_simulator
