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
#include <net-base/network_config.h>

#include "shill/adaptor_interfaces.h"
#include "shill/control_interface.h"
#include "shill/logging.h"
#include "shill/store/property_accessor.h"

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

void IPConfig::Properties::UpdateFromNetworkConfig(
    const net_base::NetworkConfig& network_config, net_base::IPFamily family) {
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
    } else {
      // Use "0.0.0.0" as empty gateway for backward compatibility.
      gateway = net_base::IPv4Address().ToString();
    }
    if (network_config.ipv4_broadcast) {
      broadcast_address = network_config.ipv4_broadcast->ToString();
    }
    default_route = network_config.ipv4_default_route;
    blackhole_ipv6 = network_config.ipv6_blackhole_route;
  }
  if (family == net_base::IPFamily::kIPv6) {
    if (!network_config.ipv6_addresses.empty()) {
      // IPConfig only supports one address.
      address = network_config.ipv6_addresses[0].address().ToString();
      subnet_prefix = network_config.ipv6_addresses[0].prefix_length();
    }
    if (network_config.ipv6_gateway) {
      gateway = network_config.ipv6_gateway->ToString();
    } else {
      // Use "::" as empty gateway for backward compatibility.
      gateway = net_base::IPv6Address().ToString();
    }
  }
  inclusion_list = {};
  for (const auto& item : network_config.included_route_prefixes) {
    if (item.GetFamily() == family) {
      inclusion_list.push_back(item.ToString());
    }
  }
  exclusion_list = {};
  for (const auto& item : network_config.excluded_route_prefixes) {
    if (item.GetFamily() == family) {
      exclusion_list.push_back(item.ToString());
    }
  }
  if (network_config.mtu) {
    mtu = *network_config.mtu;
  }
  dns_servers = {};
  for (const auto& item : network_config.dns_servers) {
    if (item.GetFamily() == family) {
      dns_servers.push_back(item.ToString());
    }
  }
  domain_search = network_config.dns_search_domains;

  if (family == net_base::IPFamily::kIPv4) {
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
  store_.RegisterConstByteArray(kiSNSOptionDataProperty,
                                &properties_.dhcp_data.isns_option_data);
  store_.RegisterDerivedUint32(
      kLeaseDurationSecondsProperty,
      Uint32Accessor(new CustomAccessor<IPConfig, uint32_t>(
          this, &IPConfig::GetLeaseDurationSeconds, nullptr)));
  SLOG(this, 2) << __func__ << " device: " << device_name_;
}

IPConfig::~IPConfig() {
  SLOG(this, 2) << __func__ << " device: " << device_name();
}

const RpcIdentifier& IPConfig::GetRpcIdentifier() const {
  return adaptor_->GetRpcIdentifier();
}

uint32_t IPConfig::GetLeaseDurationSeconds(Error* /*error*/) {
  return properties_.dhcp_data.lease_duration.InSeconds();
}

void IPConfig::ApplyNetworkConfig(
    const net_base::NetworkConfig& config,
    net_base::IPFamily family,
    const std::optional<DHCPv4Config::Data>& dhcp_data) {
  properties_.UpdateFromNetworkConfig(config, family);
  switch (family) {
    case net_base::IPFamily::kIPv6:
      properties_.method = kTypeIPv6;
      break;
    case net_base::IPFamily::kIPv4:
      if (dhcp_data) {
        properties_.UpdateFromDHCPData(*dhcp_data);
        properties_.method = kTypeDHCP;
      } else {
        properties_.method = kTypeIPv4;
      }
  }
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
         lhs.dhcp_data.lease_duration == rhs.dhcp_data.lease_duration;
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
  if (properties.dhcp_data.lease_duration.is_positive()) {
    stream << ", lease: " << properties.dhcp_data.lease_duration;
  }
  return stream << "}";
}

}  // namespace shill
