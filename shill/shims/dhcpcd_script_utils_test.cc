// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/shims/dhcpcd_script_utils.h"

#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "shill/network/dhcpv4_config.h"
#include "shill/shims/mock_environment.h"

namespace shill::shims::dhcpcd {

class DhcpcdScriptUtilsTest : public testing::Test {
 protected:
  testing::StrictMock<MockEnvironment> env_;
};

TEST_F(DhcpcdScriptUtilsTest, BuildDhcpcdConfiguration) {
  env_.ExpectVariable(kVarNamePid, "4");
  env_.ExpectVariable(kVarNameInterface, "wlan0");
  env_.ExpectVariable(kVarNameReason, "BOUND");
  env_.ExpectVariable(kVarNameIPAddress, "192.168.1.100");
  env_.ExpectVariable(kVarNameSubnetCIDR, "16");
  env_.ExpectVariable(kVarNameBroadcastAddress, "192.168.255.255");
  env_.ExpectVariable(kVarNameRouters, "192.168.1.1");
  env_.ExpectVariable(kVarNameDomainNameServers, "8.8.8.8 8.8.4.4");
  env_.ExpectVariable(kVarNameDomainName, "domain.name");
  env_.ExpectVariable(kVarNameDomainSearch, "google.com");
  env_.ExpectVariable(kVarNameInterfaceMTU, "1450");
  env_.ExpectVariable(kVarNameCaptivePortalUri,
                      "https://example.org/portal.html");
  env_.ExpectVariable(kVarNameClasslessStaticRoutes, "01020304");
  env_.ExpectVariable(kVarNameVendorEncapsulatedOptions, "05060708");
  env_.ExpectVariable(kVarNameWebProxyAutoDiscoveryUrl, "http://abc.def");
  env_.ExpectVariable(kVarNameDHCPLeaseTime, "38600");

  // No IA_PD variables are set. The loop should terminate immediately.
  env_.ExpectVariable("new_dhcp6_ia_pd1_iaid", nullptr);

  ConfigMap actual_map = BuildConfigMap(&env_);

  ConfigMap expected_map = {
      {DHCPv4Config::kConfigurationKeyPid, "4"},
      {DHCPv4Config::kConfigurationKeyInterface, "wlan0"},
      {DHCPv4Config::kConfigurationKeyReason, "BOUND"},
      {DHCPv4Config::kConfigurationKeyIPAddress, "192.168.1.100"},
      {DHCPv4Config::kConfigurationKeySubnetCIDR, "16"},
      {DHCPv4Config::kConfigurationKeyBroadcastAddress, "192.168.255.255"},
      {DHCPv4Config::kConfigurationKeyRouters, "192.168.1.1"},
      {DHCPv4Config::kConfigurationKeyDNS, "8.8.8.8 8.8.4.4"},
      {DHCPv4Config::kConfigurationKeyDomainName, "domain.name"},
      {DHCPv4Config::kConfigurationKeyDomainSearch, "google.com"},
      {DHCPv4Config::kConfigurationKeyMTU, "1450"},
      {DHCPv4Config::kConfigurationKeyCaptivePortalUri,
       "https://example.org/portal.html"},
      {DHCPv4Config::kConfigurationKeyClasslessStaticRoutes, "01020304"},
      {DHCPv4Config::kConfigurationKeyVendorEncapsulatedOptions, "05060708"},
      {DHCPv4Config::kConfigurationKeyWebProxyAutoDiscoveryUrl,
       "http://abc.def"},
      {DHCPv4Config::kConfigurationKeyLeaseTime, "38600"},
  };

  EXPECT_EQ(actual_map, expected_map);
}

TEST_F(DhcpcdScriptUtilsTest, BuildDhcpcdConfigurationWithPD) {
  // Set up standard DHCP variables.
  env_.ExpectVariable(kVarNamePid, "4");
  env_.ExpectVariable(kVarNameInterface, "wlan0");
  env_.ExpectVariable(kVarNameReason, "BOUND");
  env_.ExpectVariable(kVarNameIPAddress, nullptr);
  env_.ExpectVariable(kVarNameSubnetCIDR, nullptr);
  env_.ExpectVariable(kVarNameBroadcastAddress, nullptr);
  env_.ExpectVariable(kVarNameRouters, nullptr);
  env_.ExpectVariable(kVarNameDomainNameServers, nullptr);
  env_.ExpectVariable(kVarNameDomainName, nullptr);
  env_.ExpectVariable(kVarNameDomainSearch, nullptr);
  env_.ExpectVariable(kVarNameInterfaceMTU, "1450");
  env_.ExpectVariable(kVarNameCaptivePortalUri, nullptr);
  env_.ExpectVariable(kVarNameClasslessStaticRoutes, nullptr);
  env_.ExpectVariable(kVarNameVendorEncapsulatedOptions, nullptr);
  env_.ExpectVariable(kVarNameWebProxyAutoDiscoveryUrl, nullptr);
  env_.ExpectVariable(kVarNameDHCPLeaseTime, nullptr);

  // Set up IA_PD variables for IA 1.
  env_.ExpectVariable("new_dhcp6_ia_pd1_iaid", "2fe297f5");
  env_.ExpectVariable("new_dhcp6_ia_pd1_prefix1", "fc00:501:ffff:111::");
  env_.ExpectVariable("new_dhcp6_ia_pd1_prefix1_length", "64");
  // End of prefixes for IA 1.
  env_.ExpectVariable("new_dhcp6_ia_pd1_prefix2", nullptr);

  // Set up IA_PD variables for IA 2.
  env_.ExpectVariable("new_dhcp6_ia_pd2_iaid", "d1445192");
  env_.ExpectVariable("new_dhcp6_ia_pd2_prefix1", "2001:db8:0:101::");
  env_.ExpectVariable("new_dhcp6_ia_pd2_prefix1_length", "96");
  env_.ExpectVariable("new_dhcp6_ia_pd2_prefix2", "fc00:0:0:101::");
  env_.ExpectVariable("new_dhcp6_ia_pd2_prefix2_length", "96");
  // End of prefixes for IA 2.
  env_.ExpectVariable("new_dhcp6_ia_pd2_prefix3", nullptr);

  // End of IAs.
  env_.ExpectVariable("new_dhcp6_ia_pd3_iaid", nullptr);

  ConfigMap actual_map = BuildConfigMap(&env_);

  ConfigMap expected_map = {
      {DHCPv4Config::kConfigurationKeyPid, "4"},
      {DHCPv4Config::kConfigurationKeyInterface, "wlan0"},
      {DHCPv4Config::kConfigurationKeyReason, "BOUND"},
      {DHCPv4Config::kConfigurationKeyMTU, "1450"},
      {"IAPDPrefix.1.1", "fc00:501:ffff:111::/64"},
      {"IAPDPrefix.2.1", "2001:db8:0:101::/96"},
      {"IAPDPrefix.2.2", "fc00:0:0:101::/96"},
  };

  EXPECT_EQ(actual_map, expected_map);
}

}  // namespace shill::shims::dhcpcd
