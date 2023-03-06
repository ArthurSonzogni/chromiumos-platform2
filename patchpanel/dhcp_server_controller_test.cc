// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/dhcp_server_controller.h"

#include <base/check.h>
#include <gtest/gtest.h>
#include <shill/net/ip_address.h>

namespace patchpanel {
namespace {

using Config = DHCPServerController::Config;

// Returns a IPv4 IPAddress, or crashes if the arguments are not valid.
shill::IPAddress CreateAndUnwrapIPAddress(const std::string& ip,
                                          unsigned int prefix = 0) {
  const auto ret = shill::IPAddress::CreateFromStringAndPrefix(
      ip, prefix, shill::IPAddress::kFamilyIPv4);
  CHECK(ret.has_value()) << ip << "/" << prefix << " is not a valid IP";
  return *ret;
}

}  // namespace

class DHCPServerControllerTest : public ::testing::Test {
 protected:
  void SetUp() override {}

  DHCPServerController dhcp_server_controller_{"wlan0"};
};

TEST_F(DHCPServerControllerTest, ConfigWithWrongSubnet) {
  // start_ip and end_ip are not in 192.168.1.0 subnet.
  const auto host_ip = CreateAndUnwrapIPAddress("192.168.1.1", 24);
  const auto start_ip = CreateAndUnwrapIPAddress("192.168.5.50");
  const auto end_ip = CreateAndUnwrapIPAddress("192.168.5.100");

  EXPECT_EQ(Config::Create(host_ip, start_ip, end_ip), std::nullopt);
}

TEST_F(DHCPServerControllerTest, ConfigWithWrongRange) {
  // end_ip is smaller than start_ip.
  const auto host_ip = CreateAndUnwrapIPAddress("192.168.1.1", 24);
  const auto start_ip = CreateAndUnwrapIPAddress("192.168.1.100");
  const auto end_ip = CreateAndUnwrapIPAddress("192.168.1.50");

  EXPECT_EQ(Config::Create(host_ip, start_ip, end_ip), std::nullopt);
}

TEST_F(DHCPServerControllerTest, ValidConfig) {
  const auto host_ip = CreateAndUnwrapIPAddress("192.168.1.1", 24);
  const auto start_ip = CreateAndUnwrapIPAddress("192.168.1.50");
  const auto end_ip = CreateAndUnwrapIPAddress("192.168.1.100");
  const auto config = Config::Create(host_ip, start_ip, end_ip);

  ASSERT_NE(config, std::nullopt);
  EXPECT_EQ(config->host_ip(), "192.168.1.1");
  EXPECT_EQ(config->netmask(), "255.255.255.0");
  EXPECT_EQ(config->start_ip(), "192.168.1.50");
  EXPECT_EQ(config->end_ip(), "192.168.1.100");
}

TEST_F(DHCPServerControllerTest, Start) {
  const auto host_ip = CreateAndUnwrapIPAddress("192.168.1.1", 24);
  const auto start_ip = CreateAndUnwrapIPAddress("192.168.1.50");
  const auto end_ip = CreateAndUnwrapIPAddress("192.168.1.100");
  const auto config = Config::Create(host_ip, start_ip, end_ip).value();

  EXPECT_EQ(dhcp_server_controller_.Start(config), true);
}

}  // namespace patchpanel
