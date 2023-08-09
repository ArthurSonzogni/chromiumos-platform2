// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/ipconfig.h"

#include <algorithm>
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

template <class T>
void ApplyOptional(const std::optional<T>& src, T* dst) {
  if (src.has_value()) {
    *dst = src.value();
  }
}

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
    ret.ipv4_gateway =
        net_base::IPv4Address::CreateFromString(ipv4_prop->gateway);
    if (!ret.ipv4_gateway && !ipv4_prop->gateway.empty()) {
      LOG(WARNING) << "Ignoring invalid gateway address \""
                   << ipv4_prop->gateway << "\"";
    }
    ret.ipv4_broadcast =
        net_base::IPv4Address::CreateFromString(ipv4_prop->broadcast_address);
    if (!ret.ipv4_broadcast && !ipv4_prop->broadcast_address.empty()) {
      LOG(WARNING) << "Ignoring invalid broadcast address \""
                   << ipv4_prop->broadcast_address << "\"";
    }
    ret.ipv4_default_route = ipv4_prop->default_route;

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
  }

  // Merge included routes and excluded route from ipv4_prop and ipv6_prop.
  std::vector<const IPConfig::Properties*> non_empty_props;
  if (ipv4_prop) {
    non_empty_props.push_back(ipv4_prop);
  }
  if (ipv6_prop) {
    non_empty_props.push_back(ipv6_prop);
  }
  for (const auto* prop : non_empty_props) {
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
  // When DNS information is available from both IPv6 source (RDNSS) and IPv4
  // source (DHCPv4), the ideal behavior is happy eyeballs (RFC 8305). When
  // happy eyeballs is not implemented, the priority of DNS servers are not
  // strictly defined by standard. Prefer IPv6 here as most of the RFCs just
  // "assume" IPv6 is preferred.
  for (const auto* properties : non_empty_props) {
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
    const NetworkConfig& network_config, bool force_overwrite) {
  if (!address_family) {
    // In situations where no address is supplied (bad or missing DHCP config)
    // supply an address family ourselves.
    address_family = net_base::IPFamily::kIPv4;
  }
  if (method.empty()) {
    // When it's empty, it means there is no other IPConfig provider now (e.g.,
    // DHCP). A StaticIPParameters object is only for IPv4.
    method = kTypeIPv4;
  }

  if (address_family != net_base::IPFamily::kIPv4) {
    LOG(DFATAL) << "The IPConfig object is not for IPv4, but for "
                << address_family.value();
    return;
  }

  if (network_config.ipv4_address) {
    address = network_config.ipv4_address->address().ToString();
    subnet_prefix = network_config.ipv4_address->prefix_length();
  }
  if (network_config.ipv4_gateway) {
    gateway = network_config.ipv4_gateway->ToString();
  }
  if (network_config.ipv4_broadcast) {
    broadcast_address = network_config.ipv4_broadcast->ToString();
  }

  default_route = network_config.ipv4_default_route;

  if (force_overwrite || !network_config.included_route_prefixes.empty()) {
    inclusion_list = {};
    std::transform(network_config.included_route_prefixes.begin(),
                   network_config.included_route_prefixes.end(),
                   std::back_inserter(inclusion_list),
                   [](net_base::IPCIDR cidr) { return cidr.ToString(); });
  }
  if (force_overwrite || !network_config.excluded_route_prefixes.empty()) {
    exclusion_list = {};
    std::transform(network_config.excluded_route_prefixes.begin(),
                   network_config.excluded_route_prefixes.end(),
                   std::back_inserter(exclusion_list),
                   [](net_base::IPCIDR cidr) { return cidr.ToString(); });
  }

  ApplyOptional(network_config.mtu, &mtu);

  if (force_overwrite || !network_config.dns_servers.empty()) {
    dns_servers = {};
    std::transform(network_config.dns_servers.begin(),
                   network_config.dns_servers.end(),
                   std::back_inserter(dns_servers),
                   [](net_base::IPAddress dns) { return dns.ToString(); });
  }
  if (force_overwrite || !network_config.dns_search_domains.empty()) {
    domain_search = network_config.dns_search_domains;
  }

  if (force_overwrite || !network_config.rfc3442_routes.empty()) {
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
  store_.RegisterConstByteArray(kVendorEncapsulatedOptionsProperty,
                                &properties_.vendor_encapsulated_options);
  store_.RegisterConstString(kWebProxyAutoDiscoveryUrlProperty,
                             &properties_.web_proxy_auto_discovery);
  store_.RegisterConstUint32(kLeaseDurationSecondsProperty,
                             &properties_.lease_duration_seconds);
  store_.RegisterConstByteArray(kiSNSOptionDataProperty,
                                &properties_.isns_option_data);
  SLOG(this, 2) << __func__ << " device: " << device_name_;
}

IPConfig::~IPConfig() {
  SLOG(this, 2) << __func__ << " device: " << device_name();
}

const RpcIdentifier& IPConfig::GetRpcIdentifier() const {
  return adaptor_->GetRpcIdentifier();
}

void IPConfig::ApplyNetworkConfig(const NetworkConfig& config,
                                  bool force_overwrite) {
  properties_.UpdateFromNetworkConfig(config, force_overwrite);
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

void IPConfig::UpdateSearchDomains(
    const std::vector<std::string>& search_domains) {
  properties_.domain_search = search_domains;
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
         lhs.vendor_encapsulated_options == rhs.vendor_encapsulated_options &&
         lhs.isns_option_data == rhs.isns_option_data &&
         lhs.web_proxy_auto_discovery == rhs.web_proxy_auto_discovery &&
         lhs.lease_duration_seconds == rhs.lease_duration_seconds;
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
  if (!properties.web_proxy_auto_discovery.empty()) {
    stream << ", wpad: " << properties.web_proxy_auto_discovery;
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
  if (properties.lease_duration_seconds != 0) {
    stream << ", lease: " << properties.lease_duration_seconds << "s";
  }
  return stream << "}";
}

}  // namespace shill
