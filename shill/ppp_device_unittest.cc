// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/ppp_device.h"

#include <map>
#include <string>

#include <gtest/gtest.h>

#include "shill/mock_ppp_device.h"

using std::map;
using std::string;

namespace shill {

// TODO(quiche): Add test for UpdateIPConfigFromPPP. crbug.com/266404

TEST(PPPDeviceTest, GetInterfaceName) {
  map<string, string> config;
  config[kPPPInterfaceName] = "ppp0";
  config["foo"] = "bar";
  EXPECT_EQ("ppp0", PPPDevice::GetInterfaceName(config));
}

TEST(PPPDeviceTest, ParseIPConfiguration) {
  map<string, string> config;
  config[kPPPInternalIP4Address] = "4.5.6.7";
  config[kPPPExternalIP4Address] = "33.44.55.66";
  config[kPPPGatewayAddress] = "192.168.1.1";
  config[kPPPDNS1] = "1.1.1.1";
  config[kPPPDNS2] = "2.2.2.2";
  config[kPPPInterfaceName] = "ppp0";
  config[kPPPLNSAddress] = "99.88.77.66";
  config["foo"] = "bar";  // Unrecognized keys don't cause crash.
  IPConfig::Properties props =
      PPPDevice::ParseIPConfiguration("in-test", config);
  EXPECT_EQ(IPAddress::kFamilyIPv4, props.address_family);
  EXPECT_EQ(IPAddress::GetMaxPrefixLength(IPAddress::kFamilyIPv4),
            props.subnet_prefix);
  EXPECT_EQ("4.5.6.7", props.address);
  EXPECT_EQ("33.44.55.66", props.peer_address);
  EXPECT_EQ("192.168.1.1", props.gateway);
  ASSERT_EQ(2, props.dns_servers.size());
  EXPECT_EQ("1.1.1.1", props.dns_servers[0]);
  EXPECT_EQ("2.2.2.2", props.dns_servers[1]);
  EXPECT_EQ("99.88.77.66/32", props.exclusion_list[0]);
  EXPECT_EQ(1, props.exclusion_list.size());

  // No gateway specified.
  config.erase(kPPPGatewayAddress);
  IPConfig::Properties props2 =
      PPPDevice::ParseIPConfiguration("in-test", config);
  EXPECT_EQ("33.44.55.66", props2.gateway);
}

}  // namespace shill
