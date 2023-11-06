// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "routing-simulator/packet.h"

#include <sstream>

#include <gtest/gtest.h>
#include <net-base/ip_address.h>

namespace routing_simulator {
namespace {

// TODO(b/307460180): Add tests for |input_interface| and |source_port| to each
// test case below.

// Protocol is TCP and destination port is not given.
TEST(PacketTest, CreatePacketFromStdinIPv4TCPWithoutDstPort) {
  std::string test_input =
      "ttp\n"
      "Tcp\n"
      "168.34.34.0\n"
      "abcd\n"
      "168.98.0.0\n\n";
  // Fake an input from stdin.
  std::stringstream fake_input(test_input);
  // Fake an output from stdout.
  std::stringstream fake_output;
  auto packet = Packet::CreatePacketFromStdin(fake_input, fake_output);
  std::string actual = fake_output.str();
  std::string expected =
      "Input protocol (TCP/UDP/ICMP): "
      "Invalid protocol: ttp\n"
      "Input protocol (TCP/UDP/ICMP): "
      "Input source ip: "
      "Input destination ip: "
      "Invalid destination ip: abcd\n"
      "Input destination ip: "
      "Input destination port: "
      "No input. Destination port is set to a randomly generated number " +
      std::to_string(packet->destination_port()) + "\n";
  EXPECT_EQ(actual, expected);
  EXPECT_EQ(packet->protocol(), Packet::Protocol::kTcp);
  EXPECT_EQ(packet->source_ip(),
            net_base::IPAddress::CreateFromString("168.34.34.0").value());
  EXPECT_EQ(packet->destination_ip(),
            net_base::IPAddress::CreateFromString("168.98.0.0").value());
  EXPECT_LE(packet->destination_port(), 65535);
  EXPECT_GE(packet->destination_port(), 1024);
}

// Protocol is ICMP and this packet has no destination port.
TEST(PacketTest, CreatePacketFromStdinIPv4ICMP) {
  std::string test_input =
      "iccp\n"
      "icmp\n"
      "add\n"
      "168.34.34.0\n"
      "168.98.0.0\n";
  // Fake an input from stdin.
  std::stringstream fake_input(test_input);
  // Fake an output from stdout.
  std::stringstream fake_output;
  const auto packet = Packet::CreatePacketFromStdin(fake_input, fake_output);
  const auto actual = fake_output.str();
  const auto expected =
      "Input protocol (TCP/UDP/ICMP): "
      "Invalid protocol: iccp\n"
      "Input protocol (TCP/UDP/ICMP): "
      "Input source ip: "
      "Invalid source ip: add\n"
      "Input source ip: "
      "Input destination ip: ";
  EXPECT_EQ(actual, expected);
  EXPECT_EQ(packet->protocol(), Packet::Protocol::kIcmp);
  EXPECT_EQ(packet->source_ip(),
            net_base::IPAddress::CreateFromString("168.34.34.0").value());
  EXPECT_EQ(packet->destination_ip(),
            net_base::IPAddress::CreateFromString("168.98.0.0").value());
  EXPECT_EQ(packet->destination_port(), 0);
}

// Protocol is UDP and destination port is given.
TEST(PacketTest, CreatePacketFromStdinIPv4UDPWithDstPort) {
  std::string test_input =
      "udp\n"
      "168.34.34.0\n"
      "abcd\n"
      "168.98.0.0\n"
      "-3\n"
      "600\n";
  // Fake an input from stdin.
  std::stringstream fake_input(test_input);
  // Fake an output from stdout.
  std::stringstream fake_output;
  const auto packet = Packet::CreatePacketFromStdin(fake_input, fake_output);
  const auto actual = fake_output.str();
  const auto expected =
      "Input protocol (TCP/UDP/ICMP): "
      "Input source ip: "
      "Input destination ip: "
      "Invalid destination ip: abcd\n"
      "Input destination ip: "
      "Input destination port: "
      "Invalid destination port: -3\n"
      "Input destination port: ";
  EXPECT_EQ(actual, expected);
  EXPECT_EQ(packet->protocol(), Packet::Protocol::kUdp);
  EXPECT_EQ(packet->source_ip(),
            net_base::IPAddress::CreateFromString("168.34.34.0").value());
  EXPECT_EQ(packet->destination_ip(),
            net_base::IPAddress::CreateFromString("168.98.0.0").value());
  EXPECT_EQ(packet->destination_port(), 600);
}

// Protocol is TCP and destination port is not given.
TEST(PacketTest, CreatePacketFromStdinIPv6TCPWithoutPort) {
  std::string test_input =
      "ttp\n"
      "Tcp\n"
      "2a00:79e1:abc:f604:faac:65ff:fe56:100d\n"
      "abcd\n"
      "2b23:79e1:abc:f604:faac:65ff:fe56:1d00\n\n";
  // Fake an input from stdin.
  std::stringstream fake_input(test_input);
  // Fake an output from stdout.
  std::stringstream fake_output;
  const auto packet = Packet::CreatePacketFromStdin(fake_input, fake_output);
  const auto actual = fake_output.str();
  const auto expected =
      "Input protocol (TCP/UDP/ICMP): "
      "Invalid protocol: ttp\n"
      "Input protocol (TCP/UDP/ICMP): "
      "Input source ip: "
      "Input destination ip: "
      "Invalid destination ip: abcd\n"
      "Input destination ip: "
      "Input destination port: "
      "No input. Destination port is set to a randomly generated number " +
      std::to_string(packet->destination_port()) + "\n";
  EXPECT_EQ(actual, expected);
  EXPECT_EQ(packet->protocol(), Packet::Protocol::kTcp);
  EXPECT_EQ(packet->source_ip(), net_base::IPAddress::CreateFromString(
                                     "2a00:79e1:abc:f604:faac:65ff:fe56:100d")
                                     .value());
  EXPECT_EQ(packet->destination_ip(),
            net_base::IPAddress::CreateFromString(
                "2b23:79e1:abc:f604:faac:65ff:fe56:1d00")
                .value());
  EXPECT_LE(packet->destination_port(), 65535);
  EXPECT_GE(packet->destination_port(), 1024);
}

// Protocol is ICMP and this packet has no destination port.
TEST(PacketTest, CreatePacketFromStdinIPv6ICMP) {
  std::string test_input =
      "iccp\n"
      "icmp\n"
      "add\n"
      "2a00:79e1:abc:f604:faac:65ff:fe56:100d\n"
      "2b23:79e1:abc:f604:faac:65ff:fe56:1d00\n";
  // Fake an input from stdin.
  std::stringstream fake_input(test_input);
  // Fake an output from stdout.
  std::stringstream fake_output;
  const auto packet = Packet::CreatePacketFromStdin(fake_input, fake_output);
  const auto actual = fake_output.str();
  const auto expected =
      "Input protocol (TCP/UDP/ICMP): "
      "Invalid protocol: iccp\n"
      "Input protocol (TCP/UDP/ICMP): "
      "Input source ip: "
      "Invalid source ip: add\n"
      "Input source ip: "
      "Input destination ip: ";
  EXPECT_EQ(actual, expected);
  EXPECT_EQ(packet->protocol(), Packet::Protocol::kIcmp);
  EXPECT_EQ(packet->source_ip(), net_base::IPAddress::CreateFromString(
                                     "2a00:79e1:abc:f604:faac:65ff:fe56:100d")
                                     .value());
  EXPECT_EQ(packet->destination_ip(),
            net_base::IPAddress::CreateFromString(
                "2b23:79e1:abc:f604:faac:65ff:fe56:1d00")
                .value());
  EXPECT_EQ(packet->destination_port(), 0);
}

// Protocol is UDP and destination port is given.
TEST(PacketTest, CreatePacketFromStdinIPv6UDPWithDstPort) {
  std::string test_input =
      "udp\n"
      "2a00:79e1:abc:f604:faac:65ff:fe56:100d\n"
      "abcd\n"
      "2b23:79e1:abc:f604:faac:65ff:fe56:1d00\n"
      "-3\n"
      "600\n";
  // Fake an input from stdin.
  std::stringstream fake_input(test_input);
  // Fake an output from stdout.
  std::stringstream fake_output;
  const auto packet = Packet::CreatePacketFromStdin(fake_input, fake_output);
  const auto actual = fake_output.str();
  const auto expected =
      "Input protocol (TCP/UDP/ICMP): "
      "Input source ip: "
      "Input destination ip: "
      "Invalid destination ip: abcd\n"
      "Input destination ip: "
      "Input destination port: "
      "Invalid destination port: -3\n"
      "Input destination port: ";
  EXPECT_EQ(actual, expected);
  EXPECT_EQ(packet->protocol(), Packet::Protocol::kUdp);
  EXPECT_EQ(packet->source_ip(), net_base::IPAddress::CreateFromString(
                                     "2a00:79e1:abc:f604:faac:65ff:fe56:100d")
                                     .value());
  EXPECT_EQ(packet->destination_ip(),
            net_base::IPAddress::CreateFromString(
                "2b23:79e1:abc:f604:faac:65ff:fe56:1d00")
                .value());
  EXPECT_EQ(packet->destination_port(), 600);
}

}  // namespace
}  // namespace routing_simulator
