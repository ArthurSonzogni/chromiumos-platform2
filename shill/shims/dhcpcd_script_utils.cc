// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/shims/dhcpcd_script_utils.h"

#include <utility>

#include <base/check.h>
#include <base/strings/stringprintf.h>

#include "shill/network/dhcpv4_config.h"

namespace shill::shims::dhcpcd {

namespace {

// Key pairs between the key used by shill and the environment variable name
// used by dhcpcd.
static constexpr std::array<std::pair<std::string_view, std::string_view>, 16>
    kConfigKeyPairs = {{
        {shill::DHCPv4Config::kConfigurationKeyPid, kVarNamePid},
        {shill::DHCPv4Config::kConfigurationKeyInterface, kVarNameInterface},
        {shill::DHCPv4Config::kConfigurationKeyReason, kVarNameReason},
        {shill::DHCPv4Config::kConfigurationKeyIPAddress, kVarNameIPAddress},
        {shill::DHCPv4Config::kConfigurationKeySubnetCIDR, kVarNameSubnetCIDR},
        {shill::DHCPv4Config::kConfigurationKeyBroadcastAddress,
         kVarNameBroadcastAddress},
        {shill::DHCPv4Config::kConfigurationKeyRouters, kVarNameRouters},
        {shill::DHCPv4Config::kConfigurationKeyDNS, kVarNameDomainNameServers},
        {shill::DHCPv4Config::kConfigurationKeyDomainName, kVarNameDomainName},
        {shill::DHCPv4Config::kConfigurationKeyDomainSearch,
         kVarNameDomainSearch},
        {shill::DHCPv4Config::kConfigurationKeyMTU, kVarNameInterfaceMTU},
        {shill::DHCPv4Config::kConfigurationKeyCaptivePortalUri,
         kVarNameCaptivePortalUri},
        {shill::DHCPv4Config::kConfigurationKeyClasslessStaticRoutes,
         kVarNameClasslessStaticRoutes},
        {shill::DHCPv4Config::kConfigurationKeyVendorEncapsulatedOptions,
         kVarNameVendorEncapsulatedOptions},
        {shill::DHCPv4Config::kConfigurationKeyWebProxyAutoDiscoveryUrl,
         kVarNameWebProxyAutoDiscoveryUrl},
        {shill::DHCPv4Config::kConfigurationKeyLeaseTime,
         kVarNameDHCPLeaseTime},
    }};

}  // namespace

ConfigMap BuildConfigMap(Environment* environment) {
  CHECK(environment);

  ConfigMap config_map;
  for (const auto& [key, var_name] : kConfigKeyPairs) {
    std::string value;
    if (environment->GetVariable(std::string(var_name), &value)) {
      config_map.emplace(key, value);
    }
  }

  AppendIaPdPrefixToConfigMap(environment, &config_map);

  return config_map;
}

void AppendIaPdPrefixToConfigMap(Environment* environment,
                                 ConfigMap* config_map) {
  CHECK(environment);
  CHECK(config_map);

  for (int ia_index = 1;; ia_index++) {
    const std::string ia_id_var_name =
        base::StringPrintf("new_dhcp6_ia_pd%d_iaid", ia_index);
    std::string ia_id;
    if (!environment->GetVariable(ia_id_var_name, &ia_id)) {
      break;
    }

    for (int prefix_index = 1;; prefix_index++) {
      const std::string prefix_var_name = base::StringPrintf(
          "new_dhcp6_ia_pd%d_prefix%d", ia_index, prefix_index);
      const std::string length_var_name = base::StringPrintf(
          "new_dhcp6_ia_pd%d_prefix%d_length", ia_index, prefix_index);

      std::string prefix, length;
      if (!environment->GetVariable(prefix_var_name, &prefix) ||
          !environment->GetVariable(length_var_name, &length)) {
        break;
      }

      const std::string key =
          base::StringPrintf("IAPDPrefix.%d.%d", ia_index, prefix_index);
      const std::string value =
          base::StringPrintf("%s/%s", prefix.c_str(), length.c_str());
      config_map->emplace(key, value);
    }
  }
}

}  // namespace shill::shims::dhcpcd
