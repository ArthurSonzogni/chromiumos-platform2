// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NETWORK_DHCPV4_CONFIG_H_
#define SHILL_NETWORK_DHCPV4_CONFIG_H_

#include <string>
#include <vector>

#include <base/time/time.h>
#include <chromeos/net-base/network_config.h>

#include "shill/store/key_value_store.h"

namespace shill {

// TODO(b/350884946): Rename this class as the constants defined here are not
// limited to DHCPv4.
class DHCPv4Config {
 public:
  // The information from DHCPv4 that's not directly used in network
  // configuration but needs to be passed to user through IPConfig dbus API.
  struct Data {
    // Vendor encapsulated option string gained from DHCP.
    std::vector<uint8_t> vendor_encapsulated_options;
    // Web Proxy Auto Discovery (WPAD) URL gained from DHCP.
    std::string web_proxy_auto_discovery;
    // Length of time the lease was granted.
    base::TimeDelta lease_duration;

    friend bool operator==(const Data&, const Data&);
  };

  // Constants used as keys in the configuration got from dhcpcd. Used only
  // internally, make them public for unit tests.
  static constexpr char kConfigurationKeyBroadcastAddress[] =
      "BroadcastAddress";
  static constexpr char kConfigurationKeyCaptivePortalUri[] =
      "CaptivePortalUri";
  static constexpr char kConfigurationKeyClasslessStaticRoutes[] =
      "ClasslessStaticRoutes";
  static constexpr char kConfigurationKeyDNS[] = "DomainNameServers";
  static constexpr char kConfigurationKeyDomainName[] = "DomainName";
  static constexpr char kConfigurationKeyDomainSearch[] = "DomainSearch";
  static constexpr char kConfigurationKeyHostname[] = "Hostname";
  static constexpr char kConfigurationKeyIPAddress[] = "IPAddress";
  static constexpr char kConfigurationKeyLeaseTime[] = "DHCPLeaseTime";
  static constexpr char kConfigurationKeyMTU[] = "InterfaceMTU";
  static constexpr char kConfigurationKeyRouters[] = "Routers";
  static constexpr char kConfigurationKeySubnetCIDR[] = "SubnetCIDR";
  static constexpr char kConfigurationKeyVendorEncapsulatedOptions[] =
      "VendorEncapsulatedOptions";
  static constexpr char kConfigurationKeyWebProxyAutoDiscoveryUrl[] =
      "WebProxyAutoDiscoveryUrl";
  static constexpr char kConfigurationKeyPid[] = "Pid";
  static constexpr char kConfigurationKeyInterface[] = "Interface";
  static constexpr char kConfigurationKeyReason[] = "Reason";
  static constexpr char kConfigurationKeyPrefixIAPDPrefix[] = "IAPDPrefix";

  // Parses |configuration|. The fields that are needed for network
  // configuration are parsed into |network_config|, and the others into
  // |dhcp_data|. Returns true on success, and false otherwise.
  static bool ParseConfiguration(const KeyValueStore& configuration,
                                 net_base::NetworkConfig* network_config,
                                 DHCPv4Config::Data* dhcp_data);

  // Parses |classless_routes| into |network_config|.  Sets the default gateway
  // if one is supplied and |network_config| does not already contain one. It
  // also sets |network_config.rfc3442_routes| for all routes not converted into
  // the default gateway.  Returns true on success, and false otherwise.
  static bool ParseClasslessStaticRoutes(
      const std::string& classless_routes,
      net_base::NetworkConfig* network_config);
};

}  // namespace shill

#endif  // SHILL_NETWORK_DHCPV4_CONFIG_H_
