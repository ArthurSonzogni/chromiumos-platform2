// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "routing-simulator/packet.h"

#include <sstream>

#include <gtest/gtest.h>
#include <net-base/ip_address.h>

namespace routing_simulator {
namespace {

// Notes: When protocol is ICMP, there is no port so you cannot input string for
// a port. Otherwise, you can and if you don't input anything for a port, random
// port is generated.

// Tests the case when input string is invalid when input protocol is TCP in
// IPv4.
TEST(PacketTest, InvalidInputIPv4WithTCP) {
  std::string test_input =
      "ttp\n"
      "Tcp\n"
      "wlan0\n"
      "    \n"
      "168.34.34.0\n"
      "  \n"
      "2b23:79e1:abc:f604:faac:65ff:fe56:1d00\n"
      "168.34.0.0\n"
      "port\n"
      "1990\n"
      "0\n"
      "-3\n"
      "35\n";
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
      "Input input interface: "
      "Input destination ip: "
      "Invalid destination ip: \n"
      "Input destination ip: "
      "Input source ip: "
      "Invalid source ip: \n"
      "Input source ip: "
      "Please input source ip in IPv4\n"
      "Input source ip: "
      "Input destination port: "
      "Invalid destination port: port\n"
      "Input destination port: "
      "Input source port: "
      "Invalid source port: 0 is not from 1 to 65535\n"
      "Input source port: "
      "Invalid source port: -3 is not from 1 to 65535\n"
      "Input source port: ";
  EXPECT_EQ(actual, expected);
  EXPECT_EQ(packet.protocol(), Packet::Protocol::kTcp);
  EXPECT_EQ(packet.input_interface(), "wlan0");
  EXPECT_EQ(packet.destination_ip(),
            net_base::IPAddress::CreateFromString("168.34.34.0").value());
  EXPECT_EQ(packet.source_ip(),
            net_base::IPAddress::CreateFromString("168.34.0.0").value());
  EXPECT_EQ(packet.destination_port(), 1990);
  EXPECT_EQ(packet.source_port(), 35);
}

// Tests the case when input string is invalid when input protocol is ICMP in
// IPv4.
TEST(PacketTest, InvalidInputIPv4WithICMP) {
  std::string test_input =
      "    \n"
      "icmp\n"
      "   \n"
      "wlan0\n"
      "add\n"
      "168.34.34.0\n"
      "aaa\n"
      "168.98.0.0\n";
  // Fake an input from stdin.
  std::stringstream fake_input(test_input);
  // Fake an output from stdout.
  std::stringstream fake_output;
  const auto packet = Packet::CreatePacketFromStdin(fake_input, fake_output);
  const auto actual = fake_output.str();
  const auto expected =
      "Input protocol (TCP/UDP/ICMP): "
      "Invalid protocol: \n"
      "Input protocol (TCP/UDP/ICMP): "
      "Input input interface: "
      "Invalid input interface: only whitespaces\n"
      "Input input interface: "
      "Input destination ip: "
      "Invalid destination ip: add\n"
      "Input destination ip: "
      "Input source ip: "
      "Invalid source ip: aaa\n"
      "Input source ip: ";
  EXPECT_EQ(actual, expected);
  EXPECT_EQ(packet.protocol(), Packet::Protocol::kIcmp);
  EXPECT_EQ(packet.input_interface(), "wlan0");
  EXPECT_EQ(packet.destination_ip(),
            net_base::IPAddress::CreateFromString("168.34.34.0").value());
  EXPECT_EQ(packet.source_ip(),
            net_base::IPAddress::CreateFromString("168.98.0.0").value());
  EXPECT_EQ(packet.destination_port(), 0);
  EXPECT_EQ(packet.source_port(), 0);
}

// Tests the case when input is empty when input protocol is UDP in IPv4.
TEST(PacketTest, NoInputIPv4WithUDP) {
  std::string test_input =
      "\n"
      "udp\n"
      "\n"
      "\n"
      "168.34.34.0\n"
      "\n"
      "168.34.0.0\n"
      "\n"
      "\n";
  // Fake an input from stdin.
  std::stringstream fake_input(test_input);
  // Fake an output from stdout.
  std::stringstream fake_output;
  auto packet = Packet::CreatePacketFromStdin(fake_input, fake_output);
  std::string actual = fake_output.str();
  std::string expected =
      "Input protocol (TCP/UDP/ICMP): "
      "Invalid protocol: \n"
      "Input protocol (TCP/UDP/ICMP): "
      "Input input interface: "
      "Input interface is empty, assume it is an egress packet\n"
      "Input destination ip: "
      "Invalid destination ip: \n"
      "Input destination ip: "
      "Input source ip: "
      "Invalid source ip: \n"
      "Input source ip: "
      "Input destination port: "
      "No input: destination port is set to a randomly generated number " +
      std::to_string(packet.destination_port()) +
      "\n"
      "Input source port: "
      "No input: source port is set to a randomly generated number " +
      std::to_string(packet.source_port()) + "\n";
  EXPECT_EQ(actual, expected);
  EXPECT_EQ(packet.protocol(), Packet::Protocol::kUdp);
  EXPECT_EQ(packet.input_interface(), "");
  EXPECT_EQ(packet.destination_ip(),
            net_base::IPAddress::CreateFromString("168.34.34.0").value());
  EXPECT_EQ(packet.source_ip(),
            net_base::IPAddress::CreateFromString("168.34.0.0").value());
  // Check if the generated port is not a well-known port.
  EXPECT_LE(packet.destination_port(), 65535);
  EXPECT_GE(packet.destination_port(), 1024);
  EXPECT_LE(packet.source_port(), 65535);
  EXPECT_GE(packet.source_port(), 1024);
}

// Tests the case when input string is empty when input protocol is ICMP in
// IPv4.
TEST(PacketTest, NoInputIPv4WithICMP) {
  std::string test_input =
      "\n"
      "ICMP\n"
      "\n"
      "\n"
      "168.34.34.0\n"
      "\n"
      "168.34.0.0\n";
  // Fake an input from stdin.
  std::stringstream fake_input(test_input);
  // Fake an output from stdout.
  std::stringstream fake_output;
  auto packet = Packet::CreatePacketFromStdin(fake_input, fake_output);
  std::string actual = fake_output.str();
  std::string expected =
      "Input protocol (TCP/UDP/ICMP): "
      "Invalid protocol: \n"
      "Input protocol (TCP/UDP/ICMP): "
      "Input input interface: "
      "Input interface is empty, assume it is an egress packet\n"
      "Input destination ip: "
      "Invalid destination ip: \n"
      "Input destination ip: "
      "Input source ip: "
      "Invalid source ip: \n"
      "Input source ip: ";
  EXPECT_EQ(actual, expected);
  EXPECT_EQ(packet.protocol(), Packet::Protocol::kIcmp);
  EXPECT_EQ(packet.input_interface(), "");
  EXPECT_EQ(packet.destination_ip(),
            net_base::IPAddress::CreateFromString("168.34.34.0").value());
  EXPECT_EQ(packet.source_ip(),
            net_base::IPAddress::CreateFromString("168.34.0.0").value());
  EXPECT_EQ(packet.destination_port(), 0);
  EXPECT_EQ(packet.source_port(), 0);
}

// Tests the case when input string contains whitespaces when input protocol is
// TCP in IPv4.
TEST(PacketTest, HandleWhitespaceIPv4WithTCP) {
  std::string test_input =
      "t  tp\n"
      "TCP\n"
      "wlan 0\n"
      "wlan0\n"
      " 168 .34 .34 .0\n"
      "168.34.34.0\n"
      " 168.34.0.0\n"
      "4 5 \n"
      "45\n"
      "    899\n";
  // Fake an input from stdin.
  std::stringstream fake_input(test_input);
  // Fake an output from stdout.
  std::stringstream fake_output;
  auto packet = Packet::CreatePacketFromStdin(fake_input, fake_output);
  std::string actual = fake_output.str();
  std::string expected =
      "Input protocol (TCP/UDP/ICMP): "
      "Invalid protocol: t  tp\n"
      "Input protocol (TCP/UDP/ICMP): "
      "Input input interface: "
      "Invalid input interface: it contains whitespaces wlan 0\n"
      "Input input interface: "
      "Input destination ip: "
      "Invalid destination ip: 168 .34 .34 .0\n"
      "Input destination ip: "
      "Input source ip: "
      "Input destination port: "
      "Invalid destination port: 4 5\n"
      "Input destination port: "
      "Input source port: ";
  EXPECT_EQ(actual, expected);
  EXPECT_EQ(packet.protocol(), Packet::Protocol::kTcp);
  EXPECT_EQ(packet.input_interface(), "wlan0");
  EXPECT_EQ(packet.destination_ip(),
            net_base::IPAddress::CreateFromString("168.34.34.0").value());
  EXPECT_EQ(packet.source_ip(),
            net_base::IPAddress::CreateFromString("168.34.0.0").value());
  EXPECT_EQ(packet.destination_port(), 45);
  EXPECT_EQ(packet.source_port(), 899);
}

// Tests the case when input string contains whitespaces when input protocol is
// ICMP in IPv4.
TEST(PacketTest, HandleWhitespaceIPv4WithICMP) {
  std::string test_input =
      "ic  mp\n"
      "ICMP\n"
      "wlan0   \n"
      " 168.34.34.0 \n"
      "168.34.  0.0\n"
      "168.34.0.0\n";
  // Fake an input from stdin.
  std::stringstream fake_input(test_input);
  // Fake an output from stdout.
  std::stringstream fake_output;
  auto packet = Packet::CreatePacketFromStdin(fake_input, fake_output);
  std::string actual = fake_output.str();
  std::string expected =
      "Input protocol (TCP/UDP/ICMP): "
      "Invalid protocol: ic  mp\n"
      "Input protocol (TCP/UDP/ICMP): "
      "Input input interface: "
      "Input destination ip: "
      "Input source ip: "
      "Invalid source ip: 168.34.  0.0\n"
      "Input source ip: ";
  EXPECT_EQ(actual, expected);
  EXPECT_EQ(packet.protocol(), Packet::Protocol::kIcmp);
  EXPECT_EQ(packet.input_interface(), "wlan0");
  EXPECT_EQ(packet.destination_ip(),
            net_base::IPAddress::CreateFromString("168.34.34.0").value());
  EXPECT_EQ(packet.source_ip(),
            net_base::IPAddress::CreateFromString("168.34.0.0").value());
  EXPECT_EQ(packet.destination_port(), 0);
  EXPECT_EQ(packet.source_port(), 0);
}

// Tests the case when input string is invalid when input protocol is TCP in
// IPv6.
TEST(PacketTest, InvalidInputIPv6WithTCP) {
  std::string test_input =
      "ttp\n"
      "Tcp\n"
      "wlan0\n"
      "    \n"
      "2a00:79e1:abc:f604:faac:65ff:fe56:100d\n"
      "  \n"
      "2b23:79e1:abc:f604:faac:65ff:fe56:1d00\n"
      "port\n"
      "1990\n"
      "0\n"
      "-3\n"
      "35\n";
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
      "Input input interface: "
      "Input destination ip: "
      "Invalid destination ip: \n"
      "Input destination ip: "
      "Input source ip: "
      "Invalid source ip: \n"
      "Input source ip: "
      "Input destination port: "
      "Invalid destination port: port\n"
      "Input destination port: "
      "Input source port: "
      "Invalid source port: 0 is not from 1 to 65535\n"
      "Input source port: "
      "Invalid source port: -3 is not from 1 to 65535\n"
      "Input source port: ";
  EXPECT_EQ(actual, expected);
  EXPECT_EQ(packet.protocol(), Packet::Protocol::kTcp);
  EXPECT_EQ(packet.input_interface(), "wlan0");
  EXPECT_EQ(packet.destination_ip(),
            net_base::IPAddress::CreateFromString(
                "2a00:79e1:abc:f604:faac:65ff:fe56:100d")
                .value());
  EXPECT_EQ(packet.source_ip(), net_base::IPAddress::CreateFromString(
                                    "2b23:79e1:abc:f604:faac:65ff:fe56:1d00")
                                    .value());
  EXPECT_EQ(packet.destination_port(), 1990);
  EXPECT_EQ(packet.source_port(), 35);
}

// Tests the case when input string is invalid when input protocol is ICMP in
// IPv6.
TEST(PacketTest, InvalidInputIPv6WithICMP) {
  std::string test_input =
      "    \n"
      "icmp\n"
      "   \n"
      "wlan0\n"
      "add\n"
      "2a00:79e1:abc:f604:faac:65ff:fe56:100d\n"
      "aaa\n"
      "168.34.0.0\n"
      "2b23:79e1:abc:f604:faac:65ff:fe56:1d00\n";
  // Fake an input from stdin.
  std::stringstream fake_input(test_input);
  // Fake an output from stdout.
  std::stringstream fake_output;
  const auto packet = Packet::CreatePacketFromStdin(fake_input, fake_output);
  const auto actual = fake_output.str();
  const auto expected =
      "Input protocol (TCP/UDP/ICMP): "
      "Invalid protocol: \n"
      "Input protocol (TCP/UDP/ICMP): "
      "Input input interface: "
      "Invalid input interface: only whitespaces\n"
      "Input input interface: "
      "Input destination ip: "
      "Invalid destination ip: add\n"
      "Input destination ip: "
      "Input source ip: "
      "Invalid source ip: aaa\n"
      "Input source ip: "
      "Please input source ip in IPv6\n"
      "Input source ip: ";
  EXPECT_EQ(actual, expected);
  EXPECT_EQ(packet.protocol(), Packet::Protocol::kIcmp);
  EXPECT_EQ(packet.input_interface(), "wlan0");
  EXPECT_EQ(packet.destination_ip(),
            net_base::IPAddress::CreateFromString(
                "2a00:79e1:abc:f604:faac:65ff:fe56:100d")
                .value());
  EXPECT_EQ(packet.source_ip(), net_base::IPAddress::CreateFromString(
                                    "2b23:79e1:abc:f604:faac:65ff:fe56:1d00")
                                    .value());
  EXPECT_EQ(packet.destination_port(), 0);
  EXPECT_EQ(packet.source_port(), 0);
}

// Tests the case when input string is empty when input protocol is UCP in IPv6.
TEST(PacketTest, NoInputIPv6WithUDP) {
  std::string test_input =
      "\n"
      "udp\n"
      "\n"
      "\n"
      "2a00:79e1:abc:f604:faac:65ff:fe56:100d\n"
      "\n"
      "2b23:79e1:abc:f604:faac:65ff:fe56:1d00\n"
      "\n"
      "\n";
  // Fake an input from stdin.
  std::stringstream fake_input(test_input);
  // Fake an output from stdout.
  std::stringstream fake_output;
  auto packet = Packet::CreatePacketFromStdin(fake_input, fake_output);
  std::string actual = fake_output.str();
  std::string expected =
      "Input protocol (TCP/UDP/ICMP): "
      "Invalid protocol: \n"
      "Input protocol (TCP/UDP/ICMP): "
      "Input input interface: "
      "Input interface is empty, assume it is an egress packet\n"
      "Input destination ip: "
      "Invalid destination ip: \n"
      "Input destination ip: "
      "Input source ip: "
      "Invalid source ip: \n"
      "Input source ip: "
      "Input destination port: "
      "No input: destination port is set to a randomly generated number " +
      std::to_string(packet.destination_port()) +
      "\n"
      "Input source port: "
      "No input: source port is set to a randomly generated number " +
      std::to_string(packet.source_port()) + "\n";
  EXPECT_EQ(actual, expected);
  EXPECT_EQ(packet.protocol(), Packet::Protocol::kUdp);
  EXPECT_EQ(packet.input_interface(), "");
  EXPECT_EQ(packet.destination_ip(),
            net_base::IPAddress::CreateFromString(
                "2a00:79e1:abc:f604:faac:65ff:fe56:100d")
                .value());
  EXPECT_EQ(packet.source_ip(), net_base::IPAddress::CreateFromString(
                                    "2b23:79e1:abc:f604:faac:65ff:fe56:1d00")
                                    .value());
  // Check if the generated port is not a well-known port.
  EXPECT_LE(packet.destination_port(), 65535);
  EXPECT_GE(packet.destination_port(), 1024);
  EXPECT_LE(packet.source_port(), 65535);
  EXPECT_GE(packet.source_port(), 1024);
}

// Tests the case when input string is empty when input protocol is ICMP in
// IPv6.
TEST(PacketTest, NoInputIPv6WithICMP) {
  std::string test_input =
      "\n"
      "ICMP\n"
      "\n"
      "\n"
      "2a00:79e1:abc:f604:faac:65ff:fe56:100d\n"
      "\n"
      "2b23:79e1:abc:f604:faac:65ff:fe56:1d00\n";
  // Fake an input from stdin.
  std::stringstream fake_input(test_input);
  // Fake an output from stdout.
  std::stringstream fake_output;
  auto packet = Packet::CreatePacketFromStdin(fake_input, fake_output);
  std::string actual = fake_output.str();
  std::string expected =
      "Input protocol (TCP/UDP/ICMP): "
      "Invalid protocol: \n"
      "Input protocol (TCP/UDP/ICMP): "
      "Input input interface: "
      "Input interface is empty, assume it is an egress packet\n"
      "Input destination ip: "
      "Invalid destination ip: \n"
      "Input destination ip: "
      "Input source ip: "
      "Invalid source ip: \n"
      "Input source ip: ";
  EXPECT_EQ(actual, expected);
  EXPECT_EQ(packet.protocol(), Packet::Protocol::kIcmp);
  EXPECT_EQ(packet.input_interface(), "");
  EXPECT_EQ(packet.destination_ip(),
            net_base::IPAddress::CreateFromString(
                "2a00:79e1:abc:f604:faac:65ff:fe56:100d")
                .value());
  EXPECT_EQ(packet.source_ip(), net_base::IPAddress::CreateFromString(
                                    "2b23:79e1:abc:f604:faac:65ff:fe56:1d00")
                                    .value());
  EXPECT_EQ(packet.destination_port(), 0);
  EXPECT_EQ(packet.source_port(), 0);
}

// Tests the case when input string contains whitespaces when input protocol is
// TCP in IPv6.
TEST(PacketTest, HandleWhitespaceIPv6WithTCP) {
  std::string test_input =
      "t  tp\n"
      "TCP\n"
      "wlan 0\n"
      "wlan0\n"
      " 2a00 :79e1 :abc: f604 :faac:65ff:fe56:100d\n"
      "2a00:79e1:abc:f604:faac:65ff:fe56:100d\n"
      " 2b23:79e1:abc:f604:faac:65ff:fe56:1d00\n"
      "4 5 \n"
      "45\n"
      "    899\n";
  // Fake an input from stdin.
  std::stringstream fake_input(test_input);
  // Fake an output from stdout.
  std::stringstream fake_output;
  auto packet = Packet::CreatePacketFromStdin(fake_input, fake_output);
  std::string actual = fake_output.str();
  std::string expected =
      "Input protocol (TCP/UDP/ICMP): "
      "Invalid protocol: t  tp\n"
      "Input protocol (TCP/UDP/ICMP): "
      "Input input interface: "
      "Invalid input interface: it contains whitespaces wlan 0\n"
      "Input input interface: "
      "Input destination ip: "
      "Invalid destination ip: 2a00 :79e1 :abc: f604 :faac:65ff:fe56:100d\n"
      "Input destination ip: "
      "Input source ip: "
      "Input destination port: "
      "Invalid destination port: 4 5\n"
      "Input destination port: "
      "Input source port: ";
  EXPECT_EQ(actual, expected);
  EXPECT_EQ(packet.protocol(), Packet::Protocol::kTcp);
  EXPECT_EQ(packet.input_interface(), "wlan0");
  EXPECT_EQ(packet.destination_ip(),
            net_base::IPAddress::CreateFromString(
                "2a00:79e1:abc:f604:faac:65ff:fe56:100d")
                .value());
  EXPECT_EQ(packet.source_ip(), net_base::IPAddress::CreateFromString(
                                    "2b23:79e1:abc:f604:faac:65ff:fe56:1d00")
                                    .value());
  EXPECT_EQ(packet.destination_port(), 45);
  EXPECT_EQ(packet.source_port(), 899);
}

// Tests the case when input string contains whitespaces when input protocol is
// ICMP in IPv6.
TEST(PacketTest, HandleWhitespaceIPv6WithICMP) {
  std::string test_input =
      "ic  mp\n"
      "ICMP\n"
      "wlan0   \n"
      " 2a00:79e1:abc:f604:faac:65ff:fe56:100d \n"
      "2b23:79e1:abc:   f604:faac:65ff:fe56:1d00\n"
      "2b23:79e1:abc:f604:faac:65ff:fe56:1d00\n";
  // Fake an input from stdin.
  std::stringstream fake_input(test_input);
  // Fake an output from stdout.
  std::stringstream fake_output;
  auto packet = Packet::CreatePacketFromStdin(fake_input, fake_output);
  std::string actual = fake_output.str();
  std::string expected =
      "Input protocol (TCP/UDP/ICMP): "
      "Invalid protocol: ic  mp\n"
      "Input protocol (TCP/UDP/ICMP): "
      "Input input interface: "
      "Input destination ip: "
      "Input source ip: "
      "Invalid source ip: 2b23:79e1:abc:   f604:faac:65ff:fe56:1d00\n"
      "Input source ip: ";
  EXPECT_EQ(actual, expected);
  EXPECT_EQ(packet.protocol(), Packet::Protocol::kIcmp);
  EXPECT_EQ(packet.input_interface(), "wlan0");
  EXPECT_EQ(packet.destination_ip(),
            net_base::IPAddress::CreateFromString(
                "2a00:79e1:abc:f604:faac:65ff:fe56:100d")
                .value());
  EXPECT_EQ(packet.source_ip(), net_base::IPAddress::CreateFromString(
                                    "2b23:79e1:abc:f604:faac:65ff:fe56:1d00")
                                    .value());
  EXPECT_EQ(packet.destination_port(), 0);
  EXPECT_EQ(packet.source_port(), 0);
}

}  // namespace
}  // namespace routing_simulator
