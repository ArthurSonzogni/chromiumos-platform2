// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/ipconfig.h"

#include <algorithm>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include <base/logging.h>
#include <base/strings/strcat.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <chromeos/dbus/service_constants.h>
#include <net-base/ipv4_address.h>

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

NetworkConfig IPConfig::Properties::ToNetworkConfig() const {
  NetworkConfig ret;

  if (mtu != IPConfig::kUndefinedMTU) {
    ret.mtu = mtu;
  }
  ret.dns_search_domains = domain_search;
  for (const auto& item : dns_servers) {
    auto dns = net_base::IPAddress::CreateFromString(item);
    if (dns) {
      ret.dns_servers.push_back(*dns);
    } else {
      LOG(WARNING) << "Ignoring invalid DNS server \"" << item << "\"";
    }
  }
  for (const auto& item : inclusion_list) {
    auto cidr = net_base::IPCIDR::CreateFromCIDRString(item);
    if (cidr) {
      ret.included_route_prefixes.push_back(*cidr);
    } else {
      LOG(WARNING) << "Ignoring invalid included route \"" << item << "\"";
    }
  }
  for (const auto& item : exclusion_list) {
    auto cidr = net_base::IPCIDR::CreateFromCIDRString(item);
    if (cidr) {
      ret.excluded_route_prefixes.push_back(*cidr);
    } else {
      LOG(WARNING) << "Ignoring invalid excluded route \"" << item << "\"";
    }
  }

  if (!address_family) {
    LOG(INFO) << "The input IPConfig::Properties does not have a valid "
                 "family. Skip setting family-specific fields.";
    return ret;
  }
  switch (address_family.value()) {
    case net_base::IPFamily::kIPv4:
      ret.ipv4_address =
          net_base::IPv4CIDR::CreateFromStringAndPrefix(address, subnet_prefix);
      if (!ret.ipv4_address && !address.empty()) {
        LOG(WARNING) << "Ignoring invalid IP address \"" << address << "\"";
      }
      ret.ipv4_gateway = net_base::IPv4Address::CreateFromString(gateway);
      if (!ret.ipv4_gateway && !gateway.empty()) {
        LOG(WARNING) << "Ignoring invalid gateway address \"" << gateway
                     << "\"";
      }
      ret.ipv4_broadcast =
          net_base::IPv4Address::CreateFromString(broadcast_address);
      if (!ret.ipv4_broadcast && !broadcast_address.empty()) {
        LOG(WARNING) << "Ignoring invalid broadcast address \""
                     << broadcast_address << "\"";
      }
      ret.ipv4_default_route = default_route;
      break;
    case net_base::IPFamily::kIPv6:
      ret.ipv6_addresses = {};
      auto parsed_ipv6_address =
          net_base::IPv6CIDR::CreateFromStringAndPrefix(address, subnet_prefix);
      if (parsed_ipv6_address) {
        ret.ipv6_addresses.push_back(parsed_ipv6_address.value());
      } else if (!address.empty()) {
        LOG(WARNING) << "Ignoring invalid IP address \"" << address << "\"";
      }
      ret.ipv6_gateway = net_base::IPv6Address::CreateFromString(gateway);
      if (!ret.ipv6_gateway && !gateway.empty()) {
        LOG(WARNING) << "Ignoring invalid gateway address \"" << gateway
                     << "\"";
      }
      break;
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

NetworkConfig IPConfig::ApplyNetworkConfig(const NetworkConfig& config,
                                           bool force_overwrite) {
  auto current_config = properties_.ToNetworkConfig();
  properties_.UpdateFromNetworkConfig(config, force_overwrite);
  EmitChanges();
  return current_config;
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
