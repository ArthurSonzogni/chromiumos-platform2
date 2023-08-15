// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/ipconfig.h"

#include <vector>

#include <chromeos/dbus/service_constants.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <net-base/ip_address.h>
#include <net-base/ipv4_address.h>

#include "shill/mock_adaptors.h"
#include "shill/mock_control.h"

using testing::_;
using testing::Mock;
using testing::Return;
using testing::Test;

namespace shill {

namespace {
const char kDeviceName[] = "testdevice";
}  // namespace

class IPConfigTest : public Test {
 public:
  IPConfigTest() : ipconfig_(new IPConfig(&control_, kDeviceName)) {}

 protected:
  IPConfigMockAdaptor* GetAdaptor() {
    return static_cast<IPConfigMockAdaptor*>(ipconfig_->adaptor_.get());
  }

  void UpdateProperties(const IPConfig::Properties& properties) {
    ipconfig_->UpdateProperties(properties);
  }

  void ExpectPropertiesEqual(const IPConfig::Properties& properties) {
    EXPECT_EQ(properties.address, ipconfig_->properties().address);
    EXPECT_EQ(properties.subnet_prefix, ipconfig_->properties().subnet_prefix);
    EXPECT_EQ(properties.broadcast_address,
              ipconfig_->properties().broadcast_address);
    EXPECT_EQ(properties.dns_servers.size(),
              ipconfig_->properties().dns_servers.size());
    if (properties.dns_servers.size() ==
        ipconfig_->properties().dns_servers.size()) {
      for (size_t i = 0; i < properties.dns_servers.size(); ++i) {
        EXPECT_EQ(properties.dns_servers[i],
                  ipconfig_->properties().dns_servers[i]);
      }
    }
    EXPECT_EQ(properties.domain_search.size(),
              ipconfig_->properties().domain_search.size());
    if (properties.domain_search.size() ==
        ipconfig_->properties().domain_search.size()) {
      for (size_t i = 0; i < properties.domain_search.size(); ++i) {
        EXPECT_EQ(properties.domain_search[i],
                  ipconfig_->properties().domain_search[i]);
      }
    }
    EXPECT_EQ(properties.gateway, ipconfig_->properties().gateway);
    EXPECT_EQ(properties.blackhole_ipv6,
              ipconfig_->properties().blackhole_ipv6);
    EXPECT_EQ(properties.mtu, ipconfig_->properties().mtu);
  }

  MockControl control_;
  std::unique_ptr<IPConfig> ipconfig_;
};

TEST_F(IPConfigTest, DeviceName) {
  EXPECT_EQ(kDeviceName, ipconfig_->device_name());
}

TEST_F(IPConfigTest, UpdateProperties) {
  IPConfig::Properties properties;
  properties.address = "1.2.3.4";
  properties.subnet_prefix = 24;
  properties.broadcast_address = "11.22.33.44";
  properties.dns_servers = {"10.20.30.40", "20.30.40.50"};
  properties.domain_name = "foo.org";
  properties.domain_search = {"zoo.org", "zoo.com"};
  properties.gateway = "5.6.7.8";
  properties.blackhole_ipv6 = true;
  properties.mtu = 700;
  UpdateProperties(properties);
  ExpectPropertiesEqual(properties);

  // We should reset if ResetProperties is called.
  ipconfig_->ResetProperties();
  ExpectPropertiesEqual(IPConfig::Properties());
}

TEST_F(IPConfigTest, PropertyChanges) {
  IPConfigMockAdaptor* adaptor = GetAdaptor();

  EXPECT_CALL(*adaptor, EmitStringChanged(kAddressProperty, _));
  EXPECT_CALL(*adaptor, EmitStringsChanged(kNameServersProperty, _));
  ipconfig_->ApplyNetworkConfig({}, true);
  Mock::VerifyAndClearExpectations(adaptor);

  IPConfig::Properties ip_properties;
  EXPECT_CALL(*adaptor, EmitStringChanged(kAddressProperty, _));
  EXPECT_CALL(*adaptor, EmitStringsChanged(kNameServersProperty, _));
  UpdateProperties(ip_properties);
  Mock::VerifyAndClearExpectations(adaptor);

  EXPECT_CALL(*adaptor, EmitStringChanged(kAddressProperty, _));
  EXPECT_CALL(*adaptor, EmitStringsChanged(kNameServersProperty, _));
  ipconfig_->ResetProperties();
  Mock::VerifyAndClearExpectations(adaptor);
}

TEST(IPPropertiesTest, ToNetworkConfigDNS) {
  IPConfig::Properties ipv4_properties;
  ipv4_properties.dns_servers = {"8.8.8.8"};
  ipv4_properties.domain_search = {"domain1"};

  auto network_config =
      IPConfig::Properties::ToNetworkConfig(&ipv4_properties, nullptr);
  EXPECT_EQ(
      std::vector<net_base::IPAddress>{
          *net_base::IPAddress::CreateFromString("8.8.8.8")},
      network_config.dns_servers);
  EXPECT_EQ(std::vector<std::string>{"domain1"},
            network_config.dns_search_domains);
}

TEST(IPPropertiesTest, ToNetworkConfigDNSWithDomain) {
  IPConfig::Properties ipv4_properties;
  ipv4_properties.dns_servers = {"8.8.8.8"};
  const std::string kDomainName("chromium.org");
  ipv4_properties.domain_name = kDomainName;

  std::vector<std::string> expected_domain_search_list = {kDomainName + "."};
  auto network_config =
      IPConfig::Properties::ToNetworkConfig(&ipv4_properties, nullptr);
  EXPECT_EQ(expected_domain_search_list, network_config.dns_search_domains);
}

TEST(IPPropertiesTest, ToNetworkConfigDNSDualStack) {
  IPConfig::Properties ipv4_properties;
  ipv4_properties.dns_servers = {"8.8.8.8"};
  ipv4_properties.domain_search = {"domain1", "domain2"};
  IPConfig::Properties ipv6_properties;
  ipv6_properties.dns_servers = {"2001:4860:4860:0:0:0:0:8888"};
  ipv6_properties.domain_search = {"domain3", "domain4"};

  std::vector<net_base::IPAddress> expected_dns = {
      *net_base::IPAddress::CreateFromString("2001:4860:4860:0:0:0:0:8888"),
      *net_base::IPAddress::CreateFromString("8.8.8.8")};
  std::vector<std::string> expected_dnssl = {"domain3", "domain4", "domain1",
                                             "domain2"};
  auto network_config =
      IPConfig::Properties::ToNetworkConfig(&ipv4_properties, &ipv6_properties);
  EXPECT_EQ(expected_dns, network_config.dns_servers);
  EXPECT_EQ(expected_dnssl, network_config.dns_search_domains);
}

TEST(IPPropertiesTest, ToNetworkConfigDNSDualStackSearchListDedup) {
  IPConfig::Properties ipv4_properties;
  ipv4_properties.dns_servers = {"8.8.8.8"};
  ipv4_properties.domain_search = {"domain1", "domain2"};
  IPConfig::Properties ipv6_properties;
  ipv6_properties.dns_servers = {"2001:4860:4860:0:0:0:0:8888"};
  ipv6_properties.domain_search = {"domain1", "domain2"};

  std::vector<std::string> expected_dnssl = {"domain1", "domain2"};
  auto network_config =
      IPConfig::Properties::ToNetworkConfig(&ipv4_properties, &ipv6_properties);
  EXPECT_EQ(expected_dnssl, network_config.dns_search_domains);
}

TEST(IPPropertiesTest, ToNetworkConfigMTU) {
  // Empty value
  IPConfig::Properties properties;
  auto network_config =
      IPConfig::Properties::ToNetworkConfig(&properties, nullptr);
  EXPECT_FALSE(network_config.mtu.has_value());

  // IPv4
  properties.mtu = 1480;
  network_config = IPConfig::Properties::ToNetworkConfig(&properties, nullptr);
  EXPECT_EQ(1480, network_config.mtu);

  properties.mtu = 400;  // less than NetworkConfig::kMinIPv4MTU
  network_config = IPConfig::Properties::ToNetworkConfig(&properties, nullptr);
  EXPECT_EQ(NetworkConfig::kMinIPv4MTU, network_config.mtu);

  // IPv6
  properties.mtu = 1480;
  network_config = IPConfig::Properties::ToNetworkConfig(nullptr, &properties);
  EXPECT_EQ(1480, network_config.mtu);

  properties.mtu = 800;  // less than NetworkConfig::kMinIPv6MTU
  network_config = IPConfig::Properties::ToNetworkConfig(nullptr, &properties);
  EXPECT_EQ(NetworkConfig::kMinIPv6MTU, network_config.mtu);

  // Dual Stack
  IPConfig::Properties properties2;
  properties.mtu = 1480;
  properties2.mtu = 1400;
  network_config =
      IPConfig::Properties::ToNetworkConfig(&properties, &properties2);
  EXPECT_EQ(1400, network_config.mtu);  // the smaller of two

  properties.mtu = 800;  // less than NetworkConfig::kMinIPv6MTU
  network_config =
      IPConfig::Properties::ToNetworkConfig(&properties, &properties2);
  EXPECT_EQ(NetworkConfig::kMinIPv6MTU, network_config.mtu);
}

TEST(IPPropertiesTest, ToNetworkGateway) {
  IPConfig::Properties properties;
  properties.gateway = "192.0.2.1";
  auto network_config =
      IPConfig::Properties::ToNetworkConfig(&properties, nullptr);
  EXPECT_EQ(*net_base::IPv4Address::CreateFromString("192.0.2.1"),
            network_config.ipv4_gateway);

  // Empty gateway string means no gateway.
  properties.gateway = "";
  network_config = IPConfig::Properties::ToNetworkConfig(&properties, nullptr);
  EXPECT_EQ(std::nullopt, network_config.ipv4_gateway);

  // 0.0.0.0 also means no gateway.
  properties.gateway = "0.0.0.0";
  network_config = IPConfig::Properties::ToNetworkConfig(&properties, nullptr);
  EXPECT_EQ(std::nullopt, network_config.ipv4_gateway);

  // If peer address is set then we consider the link point-to-point and ignore
  // the gateway.
  properties.gateway = "192.0.2.1";
  properties.peer_address = "192.0.2.1";
  network_config = IPConfig::Properties::ToNetworkConfig(&properties, nullptr);
  EXPECT_EQ(std::nullopt, network_config.ipv4_gateway);

  properties.gateway = "2001:db8:100::2";
  properties.peer_address = "";
  network_config = IPConfig::Properties::ToNetworkConfig(nullptr, &properties);
  EXPECT_EQ(*net_base::IPv6Address::CreateFromString("2001:db8:100::2"),
            network_config.ipv6_gateway);

  properties.gateway = "";
  network_config = IPConfig::Properties::ToNetworkConfig(nullptr, &properties);
  EXPECT_EQ(std::nullopt, network_config.ipv6_gateway);

  properties.gateway = "::";
  network_config = IPConfig::Properties::ToNetworkConfig(nullptr, &properties);
  EXPECT_EQ(std::nullopt, network_config.ipv6_gateway);
}

}  // namespace shill
