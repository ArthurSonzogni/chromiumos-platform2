// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_SHIMS_DHCPCD_SCRIPT_UTILS_H_
#define SHILL_SHIMS_DHCPCD_SCRIPT_UTILS_H_

#include <map>
#include <string>

#include "shill/shims/environment.h"

namespace shill::shims::dhcpcd {

using ConfigMap = std::map<std::string, std::string>;

// Environment variable names from dhcpcd.
constexpr char kVarNameBroadcastAddress[] = "new_broadcast_address";
constexpr char kVarNameCaptivePortalUri[] = "new_captive_portal_uri";
constexpr char kVarNameClasslessStaticRoutes[] = "new_classless_static_routes";
constexpr char kVarNameDHCPLeaseTime[] = "new_dhcp_lease_time";
constexpr char kVarNameDomainName[] = "new_domain_name";
constexpr char kVarNameDomainNameServers[] = "new_domain_name_servers";
constexpr char kVarNameDomainSearch[] = "new_domain_search";
constexpr char kVarNameInterface[] = "interface";
constexpr char kVarNameInterfaceMTU[] = "ifmtu";
constexpr char kVarNameIPAddress[] = "new_ip_address";
constexpr char kVarNamePid[] = "pid";
constexpr char kVarNameReason[] = "reason";
constexpr char kVarNameRouters[] = "new_routers";
constexpr char kVarNameSubnetCIDR[] = "new_subnet_cidr";
constexpr char kVarNameVendorEncapsulatedOptions[] =
    "new_vendor_encapsulated_options";
constexpr char kVarNameWebProxyAutoDiscoveryUrl[] = "new_wpad_url";

// Builds a dhcpcd configuration map.
ConfigMap BuildConfigMap(Environment* environment);

// Appends IA_PD prefix fields to |config_map|.
//
// Assuming environment variables are in the format of `new_dhcp6_ia_pd1_iaid`,
// `new_dhcp6_ia_pd1_prefix1`, and `new_dhcp6_ia_pd1_prefix1_length`, the key
// format in the returned map will be `IAPDPrefix.1.1`.
void AppendIaPdPrefixToConfigMap(Environment* environment,
                                 ConfigMap* config_map);

}  // namespace shill::shims::dhcpcd

#endif  // SHILL_SHIMS_DHCPCD_SCRIPT_UTILS_H_
