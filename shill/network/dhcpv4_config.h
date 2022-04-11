// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NETWORK_DHCPV4_CONFIG_H_
#define SHILL_NETWORK_DHCPV4_CONFIG_H_

#include <string>
#include <vector>

#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#include "shill/network/dhcp_config.h"
#include "shill/technology.h"

namespace shill {

class Metrics;

// DHCPv4 client instance.
// If |hostname| is not empty, it will be used in the DHCP request as DHCP
// option 12. This asks the DHCP server to register this hostname on our
// behalf, for purposes of administration or creating a dynamic DNS entry.
class DHCPv4Config : public DHCPConfig {
 public:
  // Constants used as keys in the configuration got from dhcpcd. Used only
  // internally, make them public for unit tests.
  static constexpr char kConfigurationKeyBroadcastAddress[] =
      "BroadcastAddress";
  static constexpr char kConfigurationKeyClasslessStaticRoutes[] =
      "ClasslessStaticRoutes";
  static constexpr char kConfigurationKeyDNS[] = "DomainNameServers";
  static constexpr char kConfigurationKeyDomainName[] = "DomainName";
  static constexpr char kConfigurationKeyDomainSearch[] = "DomainSearch";
  static constexpr char kConfigurationKeyHostname[] = "Hostname";
  static constexpr char kConfigurationKeyIPAddress[] = "IPAddress";
  static constexpr char kConfigurationKeyiSNSOptionData[] = "iSNSOptionData";
  static constexpr char kConfigurationKeyLeaseTime[] = "DHCPLeaseTime";
  static constexpr char kConfigurationKeyMTU[] = "InterfaceMTU";
  static constexpr char kConfigurationKeyRouters[] = "Routers";
  static constexpr char kConfigurationKeySubnetCIDR[] = "SubnetCIDR";
  static constexpr char kConfigurationKeyVendorEncapsulatedOptions[] =
      "VendorEncapsulatedOptions";
  static constexpr char kConfigurationKeyWebProxyAutoDiscoveryUrl[] =
      "WebProxyAutoDiscoveryUrl";

  // Constants used as event type got from dhcpcd. Used only
  // internally, make them public for unit tests.
  static constexpr char kReasonBound[] = "BOUND";
  static constexpr char kReasonFail[] = "FAIL";
  static constexpr char kReasonGatewayArp[] = "GATEWAY-ARP";
  static constexpr char kReasonNak[] = "NAK";
  static constexpr char kReasonRebind[] = "REBIND";
  static constexpr char kReasonReboot[] = "REBOOT";
  static constexpr char kReasonRenew[] = "RENEW";

  DHCPv4Config(ControlInterface* control_interface,
               EventDispatcher* dispatcher,
               DHCPProvider* provider,
               const std::string& device_name,
               const std::string& lease_file_suffix,
               bool arp_gateway,
               const std::string& hostname,
               Technology technology,
               Metrics* metrics);
  DHCPv4Config(const DHCPv4Config&) = delete;
  DHCPv4Config& operator=(const DHCPv4Config&) = delete;

  ~DHCPv4Config() override;

  // Inherited from DHCPConfig.
  void ProcessEventSignal(const std::string& reason,
                          const KeyValueStore& configuration) override;

 protected:
  // Inherited from DHCPConfig.
  void CleanupClientState() override;
  bool ShouldFailOnAcquisitionTimeout() override;
  bool ShouldKeepLeaseOnDisconnect() override;
  std::vector<std::string> GetFlags() override;

 private:
  friend class DHCPConfigTest;
  friend class DHCPv4ConfigTest;
  friend class DHCPv4ConfigStaticRoutesFuzz;
  FRIEND_TEST(DHCPv4ConfigCallbackTest, ProcessEventSignalFail);
  FRIEND_TEST(DHCPv4ConfigCallbackTest, ProcessEventSignalGatewayArp);
  FRIEND_TEST(DHCPv4ConfigCallbackTest, ProcessEventSignalGatewayArpNak);
  FRIEND_TEST(DHCPv4ConfigCallbackTest, ProcessEventSignalSuccess);
  FRIEND_TEST(DHCPv4ConfigCallbackTest, ProcessEventSignalUnknown);
  FRIEND_TEST(DHCPv4ConfigCallbackTest, StoppedDuringFailureCallback);
  FRIEND_TEST(DHCPv4ConfigCallbackTest, StoppedDuringSuccessCallback);
  FRIEND_TEST(DHCPv4ConfigTest, GetIPv4AddressString);
  FRIEND_TEST(DHCPv4ConfigTest, ParseClasslessStaticRoutes);
  FRIEND_TEST(DHCPv4ConfigTest, ParseConfiguration);
  FRIEND_TEST(DHCPv4ConfigTest, ParseConfigurationWithMinimumMTU);
  FRIEND_TEST(DHCPv4ConfigTest, ProcessStatusChangeSingal);
  FRIEND_TEST(DHCPv4ConfigTest, StartWithEmptyHostname);
  FRIEND_TEST(DHCPv4ConfigTest, StartWithHostname);
  FRIEND_TEST(DHCPv4ConfigTest, StartWithVendorClass);
  FRIEND_TEST(DHCPv4ConfigTest, StartWithoutArpGateway);
  FRIEND_TEST(DHCPv4ConfigTest, StartWithoutHostname);
  FRIEND_TEST(DHCPv4ConfigTest, StartWithoutVendorClass);

  static const char kDHCPCDPathFormatPID[];

  static const char kType[];

  // Parses |classless_routes| into |properties|.  Sets the default gateway
  // if one is supplied and |properties| does not already contain one.  It
  // also sets the "routes" parameter of the IPConfig properties for all
  // routes not converted into the default gateway.  Returns true on
  // success, and false otherwise.
  static bool ParseClasslessStaticRoutes(const std::string& classless_routes,
                                         IPConfig::Properties* properties);

  // Parses |configuration| into |properties|. Returns true on success, and
  // false otherwise.
  bool ParseConfiguration(const KeyValueStore& configuration,
                          IPConfig::Properties* properties);

  // Returns the string representation of the IP address |address|, or an
  // empty string on failure.
  static std::string GetIPv4AddressString(unsigned int address);

  // Specifies whether to supply an argument to the DHCP client to validate
  // the acquired IP address using an ARP request to the gateway IP address.
  bool arp_gateway_;

  // Whether it is valid to retain the lease acquired via gateway ARP.
  bool is_gateway_arp_active_;

  // Hostname to be used in DHCP request.
  std::string hostname_;

  Metrics* metrics_;
};

}  // namespace shill

#endif  // SHILL_NETWORK_DHCPV4_CONFIG_H_
