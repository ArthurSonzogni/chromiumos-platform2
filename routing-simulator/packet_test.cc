// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "routing-simulator/packet.h"

#include <sstream>

#include <gtest/gtest.h>
#include <net-base/ip_address.h>

namespace routing_simulator {
namespace {

// TODO(b/307460180): Add tests for |input_interface|, |source_ip|,
// |destination_ip| and |source_port|.
TEST(PacketTest, CreatePacketFromStdinIPv4Success) {
  std::string expected =
      "Input protocol (TCP/UDP/ICMP): Invalid protocol: ttp\nInput protocol "
      "(TCP/UDP/ICMP): Input destination ip: Invalid destination ip: "
      "abcd\nInput destination ip: ";
  // Fake a user input from std::cin.
  std::string test_input = "ttp\nTcp\nabcd\n168.98.0.0";
  std::stringstream fake_input(test_input);
  // Fake an output from std::cout.
  std::stringstream fake_output;
  const auto packet = Packet::CreatePacketFromStdin(fake_input, fake_output);
  std::string actual = fake_output.str();
  EXPECT_EQ(actual, expected);
  EXPECT_EQ(packet->protocol(), Packet::Protocol::kTcp);
  EXPECT_EQ(packet->destination_ip(),
            net_base::IPAddress::CreateFromString("168.98.0.0").value());
}

TEST(PacketTest, CreatePacketFromStdinIPv6Success) {
  std::string expected =
      "Input protocol (TCP/UDP/ICMP): Invalid protocol: icp\nInput protocol "
      "(TCP/UDP/ICMP): Input destination ip: Invalid destination ip: "
      "abcd\nInput destination ip: ";
  // Fake a user input from std::cin.
  std::string test_input =
      "icp\nICMP\nabcd\n2a00:79e1:abc:f604:faac:65ff:fe56:100d";
  std::stringstream fake_input(test_input);
  // Fake an output from std::cout.
  std::stringstream fake_output;
  const auto packet = Packet::CreatePacketFromStdin(fake_input, fake_output);
  std::string actual = fake_output.str();
  EXPECT_EQ(actual, expected);
  EXPECT_EQ(packet->protocol(), Packet::Protocol::kIcmp);
  EXPECT_EQ(packet->destination_ip(),
            net_base::IPAddress::CreateFromString(
                "2a00:79e1:abc:f604:faac:65ff:fe56:100d")
                .value());
}

}  // namespace
}  // namespace routing_simulator
