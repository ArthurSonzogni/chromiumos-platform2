// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/ipconfig.h"

#include <algorithm>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <base/logging.h>
#include <base/strings/strcat.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <chromeos/dbus/service_constants.h>
#include <net-base/ip_address.h>
#include <net-base/ipv4_address.h>
#include <net-base/ipv6_address.h>

#include "shill/adaptor_interfaces.h"
#include "shill/control_interface.h"
#include "shill/logging.h"
#include "shill/network/network_config.h"

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kInet;
static std::string ObjectID(const IPConfig* i) {
  return i->GetRpcIdentifier().value();
}
}  // namespace Logging

namespace {

constexpr char kTypeIP[] = "ip";

}  // namespace

IPConfig::Properties::Properties() = default;
IPConfig::Properties::~Properties() = default;

bool IPConfig::Properties::HasIPAddressAndDNS() const {
  return !address.empty() && !dns_servers.empty();
}

// static
NetworkConfig IPConfig::Properties::ToNetworkConfig(
    const IPConfig::Properties* ipv4_prop,
    const IPConfig::Properties* ipv6_prop) {
  NetworkConfig ret;
  if (ipv4_prop && ipv4_prop->address_family != net_base::IPFamily::kIPv4) {
    LOG(ERROR) << "Expecting IPv4 config, seeing "
               << (ipv4_prop->address_family
                       ? net_base::ToString(*ipv4_prop->address_family)
                       : "none");
  }
  if (ipv6_prop && ipv6_prop->address_family != net_base::IPFamily::kIPv6) {
    LOG(ERROR) << "Expecting IPv6 config, seeing "
               << (ipv6_prop->address_family
                       ? net_base::ToString(*ipv6_prop->address_family)
                       : "none");
  }

  // IPv4 address/gateway configurations from ipv4_prop.
  if (ipv4_prop) {
    ret.ipv4_address = net_base::IPv4CIDR::CreateFromStringAndPrefix(
        ipv4_prop->address, ipv4_prop->subnet_prefix);
    if (!ret.ipv4_address && !ipv4_prop->address.empty()) {
      LOG(WARNING) << "Ignoring invalid IP address \"" << ipv4_prop->address
                   << "/" << ipv4_prop->subnet_prefix << "\"";
    }
    // Empty means no gateway, a valid value for p2p networks. Present of
    // |peer_address| also suggest that this is a p2p network. Also accepts
    // "0.0.0.0" and "::" as indicators of no gateway.
    ret.ipv4_gateway =
        net_base::IPv4Address::CreateFromString(ipv4_prop->gateway);
    if (!ret.ipv4_gateway && !ipv4_prop->gateway.empty()) {
      LOG(WARNING) << "Ignoring invalid gateway address \""
                   << ipv4_prop->gateway << "\"";
    }
    if (ret.ipv4_gateway && ret.ipv4_gateway->IsZero()) {
      ret.ipv4_gateway = std::nullopt;
    }
    if (!ipv4_prop->peer_address.empty()) {
      const auto peer =
          net_base::IPAddress::CreateFromString(ipv4_prop->peer_address);
      if (!peer) {
        LOG(ERROR) << "Ignoring invalid peer address \""
                   << ipv4_prop->peer_address << "\"";
      } else {
        ret.ipv4_gateway = std::nullopt;
      }
    }
    ret.ipv4_broadcast =
        net_base::IPv4Address::CreateFromString(ipv4_prop->broadcast_address);
    if (!ret.ipv4_broadcast && !ipv4_prop->broadcast_address.empty()) {
      LOG(WARNING) << "Ignoring invalid broadcast address \""
                   << ipv4_prop->broadcast_address << "\"";
    }
    ret.ipv4_default_route = ipv4_prop->default_route;
    ret.ipv6_blackhole_route = ipv4_prop->blackhole_ipv6;

    for (const auto& route : ipv4_prop->dhcp_classless_static_routes) {
      auto prefix = net_base::IPv4CIDR::CreateFromStringAndPrefix(route.host,
                                                                  route.prefix);
      if (!prefix) {
        LOG(WARNING) << "Invalid RFC3442 route destination " << route.host
                     << "/" << route.prefix;
        continue;
      }
      auto gateway = net_base::IPv4Address::CreateFromString(route.gateway);
      if (!gateway) {
        LOG(WARNING) << "Invalid RFC3442 route gateway " << route.gateway;
        continue;
      }
      ret.rfc3442_routes.emplace_back(*prefix, *gateway);
    }
  }

  // IPv6 address/gateway configurations from ipv6_prop.
  if (ipv6_prop) {
    ret.ipv6_addresses = {};
    auto parsed_ipv6_address = net_base::IPv6CIDR::CreateFromStringAndPrefix(
        ipv6_prop->address, ipv6_prop->subnet_prefix);
    if (parsed_ipv6_address) {
      ret.ipv6_addresses.push_back(parsed_ipv6_address.value());
    } else if (!ipv6_prop->address.empty()) {
      LOG(WARNING) << "Ignoring invalid IP address \"" << ipv6_prop->address
                   << "/" << ipv6_prop->subnet_prefix << "\"";
    }
    ret.ipv6_gateway =
        net_base::IPv6Address::CreateFromString(ipv6_prop->gateway);
    if (!ret.ipv6_gateway && !ipv6_prop->gateway.empty()) {
      LOG(WARNING) << "Ignoring invalid gateway address \""
                   << ipv6_prop->gateway << "\"";
    }
    if (ret.ipv6_gateway && ret.ipv6_gateway->IsZero()) {
      // Some VPNs use all-zero to represent no gateway.
      ret.ipv6_gateway = std::nullopt;
    }
  }

  // Merge included routes and excluded route from ipv4_prop and ipv6_prop.
  for (const auto* prop : {ipv4_prop, ipv6_prop}) {
    if (!prop) {
      continue;
    }
    for (const auto& item : prop->inclusion_list) {
      auto cidr = net_base::IPCIDR::CreateFromCIDRString(item);
      if (cidr) {
        ret.included_route_prefixes.push_back(*cidr);
      } else {
        LOG(WARNING) << "Ignoring invalid included route \"" << item << "\"";
      }
    }
    for (const auto& item : prop->exclusion_list) {
      auto cidr = net_base::IPCIDR::CreateFromCIDRString(item);
      if (cidr) {
        ret.excluded_route_prefixes.push_back(*cidr);
      } else {
        LOG(WARNING) << "Ignoring invalid excluded route \"" << item << "\"";
      }
    }
  }

  // Merge DNS and DNSSL from ipv4_prop and ipv6_prop.
  std::set<std::string> domain_search_dedup;
  // When DNS information is available from both IPv6 source and IPv4 source,
  // the ideal behavior is happy eyeballs (RFC 8305). When happy eyeballs is not
  // implemented, the priority of DNS servers are not strictly defined by
  // standard. Put IPv6 in front here as most of the RFCs just "assume" IPv6 is
  // preferred.
  for (const auto* properties : {ipv6_prop, ipv4_prop}) {
    if (!properties) {
      continue;
    }
    for (const auto& item : properties->dns_servers) {
      auto dns = net_base::IPAddress::CreateFromString(item);
      if (dns) {
        ret.dns_servers.push_back(*dns);
      } else {
        LOG(WARNING) << "Ignoring invalid DNS server \"" << item << "\"";
      }
    }
    for (const auto& item : properties->domain_search) {
      if (domain_search_dedup.count(item) == 0) {
        ret.dns_search_domains.push_back(item);
        domain_search_dedup.insert(item);
      }
    }
    if (properties->domain_search.empty() && !properties->domain_name.empty()) {
      auto search_list_derived = properties->domain_name + ".";
      if (domain_search_dedup.count(search_list_derived) == 0) {
        ret.dns_search_domains.push_back(search_list_derived);
        domain_search_dedup.insert(search_list_derived);
      }
    }
  }

  // Merge MTU from ipv4_prop and ipv6_prop.
  int mtu = INT32_MAX;
  if (ipv4_prop && ipv4_prop->mtu > 0) {
    mtu = std::min(mtu, ipv4_prop->mtu);
  }
  if (ipv6_prop && ipv6_prop->mtu > 0) {
    mtu = std::min(mtu, ipv6_prop->mtu);
  }
  int min_mtu =
      ipv6_prop ? NetworkConfig::kMinIPv6MTU : NetworkConfig::kMinIPv4MTU;
  if (mtu < min_mtu) {
    LOG(INFO) << __func__ << " MTU " << mtu << " is too small; adjusting up to "
              << min_mtu;
    mtu = min_mtu;
  }
  if (mtu != INT32_MAX) {
    ret.mtu = mtu;
  }

  return ret;
}

void IPConfig::Properties::UpdateFromNetworkConfig(
    const NetworkConfig& network_config,
    bool force_overwrite,
    net_base::IPFamily family) {
  if (!address_family) {
    // In situations where no address is supplied (bad or missing DHCP config)
    // supply an address family ourselves.
    address_family = family;
  }
  if (address_family != family) {
    LOG(DFATAL) << "The IPConfig object is not for " << family << ", but for "
                << address_family.value();
    return;
  }
  if (method.empty()) {
    // When it's empty, it means there is no other IPConfig provider now (e.g.,
    // DHCP). A StaticIPParameters object is only for IPv4.
    method =
        address_family == net_base::IPFamily::kIPv6 ? kTypeIPv6 : kTypeIPv4;
  }
  if (family == net_base::IPFamily::kIPv4) {
    if (network_config.ipv4_address) {
      address = network_config.ipv4_address->address().ToString();
      subnet_prefix = network_config.ipv4_address->prefix_length();
    }
    if (network_config.ipv4_gateway) {
      gateway = network_config.ipv4_gateway->ToString();
    } else if (force_overwrite) {
      // Use "0.0.0.0" as empty gateway for backward compatibility.
      gateway = net_base::IPv4Address().ToString();
    }
    if (network_config.ipv4_broadcast) {
      broadcast_address = network_config.ipv4_broadcast->ToString();
    }
    if (force_overwrite || !network_config.ipv4_default_route) {
      default_route = network_config.ipv4_default_route;
    }
    if (force_overwrite || network_config.ipv6_blackhole_route) {
      blackhole_ipv6 = network_config.ipv6_blackhole_route;
    }
  }
  if (family == net_base::IPFamily::kIPv6) {
    if (!network_config.ipv6_addresses.empty()) {
      // IPConfig only supports one address.
      address = network_config.ipv6_addresses[0].address().ToString();
      subnet_prefix = network_config.ipv6_addresses[0].prefix_length();
    }
    if (network_config.ipv6_gateway) {
      gateway = network_config.ipv6_gateway->ToString();
    } else if (force_overwrite) {
      // Use "::" as empty gateway for backward compatibility.
      gateway = net_base::IPv6Address().ToString();
    }
  }

  if (force_overwrite || !network_config.included_route_prefixes.empty()) {
    inclusion_list = {};
    for (const auto& item : network_config.included_route_prefixes) {
      if (item.GetFamily() == family) {
        inclusion_list.push_back(item.ToString());
      }
    }
  }
  if (force_overwrite || !network_config.excluded_route_prefixes.empty()) {
    exclusion_list = {};
    for (const auto& item : network_config.excluded_route_prefixes) {
      if (item.GetFamily() == family) {
        exclusion_list.push_back(item.ToString());
      }
    }
  }

  if (network_config.mtu) {
    mtu = *network_config.mtu;
  }

  if (force_overwrite || !network_config.dns_servers.empty()) {
    dns_servers = {};
    for (const auto& item : network_config.dns_servers) {
      if (item.GetFamily() == family) {
        dns_servers.push_back(item.ToString());
      }
    }
  }
  if (force_overwrite || !network_config.dns_search_domains.empty()) {
    domain_search = network_config.dns_search_domains;
  }

  if (family == net_base::IPFamily::kIPv4 &&
      (force_overwrite || !network_config.rfc3442_routes.empty())) {
    dhcp_classless_static_routes = {};
    std::transform(
        network_config.rfc3442_routes.begin(),
        network_config.rfc3442_routes.end(),
        std::back_inserter(dhcp_classless_static_routes),
        [](const std::pair<net_base::IPv4CIDR, net_base::IPv4Address>&
               rfc3442_route) {
          return IPConfig::Route(rfc3442_route.first.address().ToString(),
                                 rfc3442_route.first.prefix_length(),
                                 rfc3442_route.second.ToString());
        });
  }
}

void IPConfig::Properties::UpdateFromDHCPData(
    const DHCPv4Config::Data& dhcp_data) {
  this->dhcp_data = dhcp_data;
}

// static
uint32_t IPConfig::global_serial_ = 0;

IPConfig::IPConfig(ControlInterface* control_interface,
                   const std::string& device_name)
    : IPConfig(control_interface, device_name, kTypeIP) {}

IPConfig::IPConfig(ControlInterface* control_interface,
                   const std::string& device_name,
                   const std::string& type)
    : device_name_(device_name),
      type_(type),
      serial_(global_serial_++),
      adaptor_(control_interface->CreateIPConfigAdaptor(this)) {
  store_.RegisterConstString(kAddressProperty, &properties_.address);
  store_.RegisterConstString(kBroadcastProperty,
                             &properties_.broadcast_address);
  store_.RegisterConstString(kDomainNameProperty, &properties_.domain_name);
  store_.RegisterConstString(kGatewayProperty, &properties_.gateway);
  store_.RegisterConstString(kMethodProperty, &properties_.method);
  store_.RegisterConstInt32(kMtuProperty, &properties_.mtu);
  store_.RegisterConstStrings(kNameServersProperty, &properties_.dns_servers);
  store_.RegisterConstString(kPeerAddressProperty, &properties_.peer_address);
  store_.RegisterConstInt32(kPrefixlenProperty, &properties_.subnet_prefix);
  store_.RegisterConstStrings(kSearchDomainsProperty,
                              &properties_.domain_search);
  store_.RegisterConstByteArray(
      kVendorEncapsulatedOptionsProperty,
      &properties_.dhcp_data.vendor_encapsulated_options);
  store_.RegisterConstString(kWebProxyAutoDiscoveryUrlProperty,
                             &properties_.dhcp_data.web_proxy_auto_discovery);
  store_.RegisterConstUint32(kLeaseDurationSecondsProperty,
                             &properties_.dhcp_data.lease_duration_seconds);
  store_.RegisterConstByteArray(kiSNSOptionDataProperty,
                                &properties_.dhcp_data.isns_option_data);
  SLOG(this, 2) << __func__ << " device: " << device_name_;
}

IPConfig::~IPConfig() {
  SLOG(this, 2) << __func__ << " device: " << device_name();
}

const RpcIdentifier& IPConfig::GetRpcIdentifier() const {
  return adaptor_->GetRpcIdentifier();
}

void IPConfig::ApplyNetworkConfig(const NetworkConfig& config,
                                  bool force_overwrite,
                                  net_base::IPFamily family) {
  properties_.UpdateFromNetworkConfig(config, force_overwrite, family);
  EmitChanges();
}

void IPConfig::UpdateFromDHCP(const NetworkConfig& config,
                              const DHCPv4Config::Data& dhcp_data) {
  properties_.method = kTypeDHCP;
  properties_.UpdateFromDHCPData(dhcp_data);
  properties_.UpdateFromNetworkConfig(config, true, net_base::IPFamily::kIPv4);
  EmitChanges();
}

void IPConfig::UpdateProperties(const Properties& properties) {
  properties_ = properties;
  EmitChanges();
}

void IPConfig::UpdateDNSServers(std::vector<std::string> dns_servers) {
  properties_.dns_servers = std::move(dns_servers);
  EmitChanges();
}

void IPConfig::ResetProperties() {
  properties_ = Properties();
  EmitChanges();
}

void IPConfig::EmitChanges() {
  adaptor_->EmitStringChanged(kAddressProperty, properties_.address);
  adaptor_->EmitStringsChanged(kNameServersProperty, properties_.dns_servers);
}

bool operator==(const IPConfig::Route& lhs, const IPConfig::Route& rhs) {
  return lhs.host == rhs.host && lhs.prefix == rhs.prefix &&
         lhs.gateway == rhs.gateway;
}

// TODO(b/232177767): Ignore the order for vector properties.
bool operator==(const IPConfig::Properties& lhs,
                const IPConfig::Properties& rhs) {
  return lhs.address_family == rhs.address_family &&
         lhs.address == rhs.address && lhs.subnet_prefix == rhs.subnet_prefix &&
         lhs.broadcast_address == rhs.broadcast_address &&
         lhs.dns_servers == rhs.dns_servers &&
         lhs.domain_name == rhs.domain_name &&
         lhs.domain_search == rhs.domain_search && lhs.gateway == rhs.gateway &&
         lhs.method == rhs.method && lhs.peer_address == rhs.peer_address &&
         lhs.default_route == rhs.default_route &&
         lhs.inclusion_list == rhs.inclusion_list &&
         lhs.exclusion_list == rhs.exclusion_list &&
         lhs.blackhole_ipv6 == rhs.blackhole_ipv6 && lhs.mtu == rhs.mtu &&
         lhs.dhcp_classless_static_routes == rhs.dhcp_classless_static_routes &&
         lhs.dhcp_data.vendor_encapsulated_options ==
             rhs.dhcp_data.vendor_encapsulated_options &&
         lhs.dhcp_data.isns_option_data == rhs.dhcp_data.isns_option_data &&
         lhs.dhcp_data.web_proxy_auto_discovery ==
             rhs.dhcp_data.web_proxy_auto_discovery &&
         lhs.dhcp_data.lease_duration_seconds ==
             rhs.dhcp_data.lease_duration_seconds;
}

std::ostream& operator<<(std::ostream& stream, const IPConfig& config) {
  return stream << config.properties();
}

std::ostream& operator<<(std::ostream& stream,
                         const IPConfig::Properties& properties) {
  stream << "{address: " << properties.address << "/"
         << properties.subnet_prefix << ", gateway: " << properties.gateway;
  if (!properties.peer_address.empty()) {
    stream << ", peer_address: " << properties.peer_address;
  }
  if (!properties.inclusion_list.empty()) {
    stream << ", included routes: ["
           << base::JoinString(properties.inclusion_list, ",") << "]";
  }
  if (!properties.exclusion_list.empty()) {
    stream << ", excluded routes: ["
           << base::JoinString(properties.exclusion_list, ",") << "]";
  }
  if (!properties.dns_servers.empty()) {
    stream << ", DNS: [" << base::JoinString(properties.dns_servers, ",")
           << "]";
  }
  if (!properties.domain_search.empty()) {
    stream << ", search domains: ["
           << base::JoinString(properties.domain_search, ",") << "]";
  }
  if (!properties.domain_name.empty()) {
    stream << ", domain name: " << properties.domain_name;
  }
  if (!properties.dhcp_data.web_proxy_auto_discovery.empty()) {
    stream << ", wpad: " << properties.dhcp_data.web_proxy_auto_discovery;
  }
  if (properties.default_route) {
    stream << ", default_route: true";
  }
  if (properties.blackhole_ipv6) {
    stream << ", blackhole_ipv6: true";
  }
  if (properties.mtu != IPConfig::kUndefinedMTU) {
    stream << ", mtu: " << properties.mtu;
  }
  if (properties.dhcp_data.lease_duration_seconds != 0) {
    stream << ", lease: " << properties.dhcp_data.lease_duration_seconds << "s";
  }
  return stream << "}";
}

}  // namespace shill
