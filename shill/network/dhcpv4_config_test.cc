// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/dhcpv4_config.h"

#include <memory>
#include <string>

#include <chromeos/dbus/service_constants.h>

#include "shill/event_dispatcher.h"
#include "shill/network/mock_dhcp_provider.h"
#include "shill/store/fake_store.h"
#include "shill/store/property_store_test.h"
#include "shill/technology.h"
#include "shill/testing.h"

using testing::_;
using testing::Mock;

namespace shill {

namespace {
const char kDeviceName[] = "eth0";
const char kLeaseFileSuffix[] = "leasefilesuffix";
const bool kArpGateway = true;
}  // namespace

using DHCPv4ConfigRefPtr = scoped_refptr<DHCPv4Config>;

class DHCPv4ConfigTest : public PropertyStoreTest {
 public:
  DHCPv4ConfigTest()
      : config_(new DHCPv4Config(control_interface(),
                                 dispatcher(),
                                 &provider_,
                                 kDeviceName,
                                 kLeaseFileSuffix,
                                 kArpGateway,
                                 "",
                                 Technology::kUnknown,
                                 metrics())) {}

 protected:
  MockDHCPProvider provider_;
  DHCPv4ConfigRefPtr config_;
};

TEST_F(DHCPv4ConfigTest, GetIPv4AddressString) {
  EXPECT_EQ("255.255.255.255", DHCPv4Config::GetIPv4AddressString(0xffffffff));
  EXPECT_EQ("0.0.0.0", DHCPv4Config::GetIPv4AddressString(0));
  EXPECT_EQ("1.2.3.4", DHCPv4Config::GetIPv4AddressString(0x04030201));
}

TEST_F(DHCPv4ConfigTest, ParseClasslessStaticRoutes) {
  const std::string kDefaultAddress = "0.0.0.0";
  const std::string kDefaultDestination = kDefaultAddress + "/0";
  const std::string kRouter0 = "10.0.0.254";
  const std::string kAddress1 = "192.168.1.0";
  const std::string kDestination1 = kAddress1 + "/24";
  // Last gateway missing, leaving an odd number of parameters.
  const std::string kBrokenClasslessRoutes0 =
      kDefaultDestination + " " + kRouter0 + " " + kDestination1;
  IPConfig::Properties properties;
  EXPECT_FALSE(DHCPv4Config::ParseClasslessStaticRoutes(kBrokenClasslessRoutes0,
                                                        &properties));
  EXPECT_TRUE(properties.routes.empty());
  EXPECT_TRUE(properties.included_dsts.empty());
  EXPECT_TRUE(properties.gateway.empty());

  // Gateway argument for the second route is malformed, but we were able
  // to salvage a default gateway.
  const std::string kBrokenRouter1 = "10.0.0";
  const std::string kBrokenClasslessRoutes1 =
      kBrokenClasslessRoutes0 + " " + kBrokenRouter1;
  EXPECT_FALSE(DHCPv4Config::ParseClasslessStaticRoutes(kBrokenClasslessRoutes1,
                                                        &properties));
  EXPECT_TRUE(properties.routes.empty());
  EXPECT_TRUE(properties.included_dsts.empty());
  EXPECT_EQ(kRouter0, properties.gateway);

  const std::string kRouter1 = "10.0.0.253";
  const std::string kRouter2 = "10.0.0.252";
  const std::string kClasslessRoutes0 = kDefaultDestination + " " + kRouter2 +
                                        " " + kDestination1 + " " + kRouter1;
  EXPECT_TRUE(
      DHCPv4Config::ParseClasslessStaticRoutes(kClasslessRoutes0, &properties));
  // The old default route is preserved.
  EXPECT_EQ(kRouter0, properties.gateway);

  // The two routes (including the one which would have otherwise been
  // classified as a default route) are added to the routing table.
  EXPECT_EQ(2, properties.routes.size());
  EXPECT_EQ(2, properties.included_dsts.size());
  const IPConfig::Route& route0 = properties.routes[0];
  EXPECT_EQ(kDefaultAddress, route0.host);
  EXPECT_EQ(0, route0.prefix);
  EXPECT_EQ(kRouter2, route0.gateway);

  const IPConfig::Route& route1 = properties.routes[1];
  EXPECT_EQ(kAddress1, route1.host);
  EXPECT_EQ(24, route1.prefix);
  EXPECT_EQ(kRouter1, route1.gateway);

  // A malformed routing table should not affect the current table.
  EXPECT_FALSE(DHCPv4Config::ParseClasslessStaticRoutes(kBrokenClasslessRoutes1,
                                                        &properties));
  EXPECT_EQ(2, properties.routes.size());
  EXPECT_EQ(2, properties.included_dsts.size());
  EXPECT_EQ(kRouter0, properties.gateway);
}

TEST_F(DHCPv4ConfigTest, ParseConfiguration) {
  KeyValueStore conf;
  conf.Set<uint32_t>(DHCPv4Config::kConfigurationKeyIPAddress, 0x01020304);
  conf.Set<uint8_t>(DHCPv4Config::kConfigurationKeySubnetCIDR, 16);
  conf.Set<uint32_t>(DHCPv4Config::kConfigurationKeyBroadcastAddress,
                     0x10203040);
  conf.Set<std::vector<uint32_t>>(DHCPv4Config::kConfigurationKeyRouters,
                                  {0x02040608, 0x03050709});
  conf.Set<std::vector<uint32_t>>(DHCPv4Config::kConfigurationKeyDNS,
                                  {0x09070503, 0x08060402});
  conf.Set<std::string>(DHCPv4Config::kConfigurationKeyDomainName,
                        "domain-name");
  conf.Set<Strings>(DHCPv4Config::kConfigurationKeyDomainSearch,
                    {"foo.com", "bar.com"});
  conf.Set<uint16_t>(DHCPv4Config::kConfigurationKeyMTU, 600);
  conf.Set<std::string>(DHCPv4Config::kConfigurationKeyHostname, "hostname");
  conf.Set<std::string>("UnknownKey", "UnknownValue");

  ByteArray isns_data{0x1, 0x2, 0x3, 0x4};
  conf.Set<std::vector<uint8_t>>(DHCPv4Config::kConfigurationKeyiSNSOptionData,
                                 isns_data);

  IPConfig::Properties properties;
  ASSERT_TRUE(config_->ParseConfiguration(conf, &properties));
  EXPECT_EQ("4.3.2.1", properties.address);
  EXPECT_EQ(16, properties.subnet_prefix);
  EXPECT_EQ("64.48.32.16", properties.broadcast_address);
  EXPECT_EQ("8.6.4.2", properties.gateway);
  ASSERT_EQ(2, properties.dns_servers.size());
  EXPECT_EQ("3.5.7.9", properties.dns_servers[0]);
  EXPECT_EQ("2.4.6.8", properties.dns_servers[1]);
  EXPECT_EQ("domain-name", properties.domain_name);
  ASSERT_EQ(2, properties.domain_search.size());
  EXPECT_EQ("foo.com", properties.domain_search[0]);
  EXPECT_EQ("bar.com", properties.domain_search[1]);
  EXPECT_EQ(600, properties.mtu);
  EXPECT_EQ("hostname", properties.accepted_hostname);
  EXPECT_EQ(isns_data.size(), properties.isns_option_data.size());
  EXPECT_FALSE(
      memcmp(&properties.isns_option_data[0], &isns_data[0], isns_data.size()));
}

TEST_F(DHCPv4ConfigTest, ParseConfigurationWithMinimumMTU) {
  // Even without a minimum MTU set, we should ignore a 576 value.
  KeyValueStore conf;
  conf.Set<uint16_t>(DHCPv4Config::kConfigurationKeyMTU, 576);

  IPConfig::Properties properties;
  ASSERT_TRUE(config_->ParseConfiguration(conf, &properties));
  EXPECT_EQ(IPConfig::kUndefinedMTU, properties.mtu);

  // With a minimum MTU set, values below the minimum should be ignored.
  config_->set_minimum_mtu(1500);
  conf.Remove(DHCPv4Config::kConfigurationKeyMTU);
  conf.Set<uint16_t>(DHCPv4Config::kConfigurationKeyMTU, 1499);
  ASSERT_TRUE(config_->ParseConfiguration(conf, &properties));
  EXPECT_EQ(IPConfig::kUndefinedMTU, properties.mtu);

  // A value (other than 576) should be accepted if it is >= mimimum_mtu.
  config_->set_minimum_mtu(577);
  conf.Remove(DHCPv4Config::kConfigurationKeyMTU);
  conf.Set<uint16_t>(DHCPv4Config::kConfigurationKeyMTU, 577);
  ASSERT_TRUE(config_->ParseConfiguration(conf, &properties));
  EXPECT_EQ(577, properties.mtu);
}

}  // namespace shill
