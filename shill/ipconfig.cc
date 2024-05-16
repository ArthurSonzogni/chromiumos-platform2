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
  store_.RegisterConstString(kGatewayProperty, &properties_.gateway);
  store_.RegisterConstString(kMethodProperty, &properties_.method);
  store_.RegisterConstInt32(kMtuProperty, &properties_.mtu);
  store_.RegisterConstStrings(kNameServersProperty, &properties_.dns_servers);
  store_.RegisterConstInt32(kPrefixlenProperty, &properties_.subnet_prefix);
  store_.RegisterConstStrings(kSearchDomainsProperty,
                              &properties_.domain_search);
  store_.RegisterConstString(kWebProxyAutoDiscoveryUrlProperty,
                             &properties_.dhcp_data.web_proxy_auto_discovery);
  SLOG(this, 2) << __func__ << " device: " << device_name_;
}

IPConfig::~IPConfig() {
  SLOG(this, 2) << __func__ << " device: " << device_name();
}

const RpcIdentifier& IPConfig::GetRpcIdentifier() const {
  return adaptor_->GetRpcIdentifier();
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

// TODO(b/232177767): Ignore the order for vector properties.
bool operator==(const IPConfig::Properties& lhs,
                const IPConfig::Properties& rhs) {
  return lhs.address_family == rhs.address_family &&
         lhs.address == rhs.address && lhs.subnet_prefix == rhs.subnet_prefix &&
         lhs.dns_servers == rhs.dns_servers &&
         lhs.domain_search == rhs.domain_search && lhs.gateway == rhs.gateway &&
         lhs.method == rhs.method && lhs.mtu == rhs.mtu &&
         lhs.dhcp_data.web_proxy_auto_discovery ==
             rhs.dhcp_data.web_proxy_auto_discovery;
}

std::ostream& operator<<(std::ostream& stream, const IPConfig& config) {
  return stream << config.properties();
}

std::ostream& operator<<(std::ostream& stream,
                         const IPConfig::Properties& properties) {
  stream << "{address: " << properties.address << "/"
         << properties.subnet_prefix << ", gateway: " << properties.gateway;
  if (!properties.dns_servers.empty()) {
    stream << ", DNS: [" << base::JoinString(properties.dns_servers, ",")
           << "]";
  }
  if (!properties.domain_search.empty()) {
    stream << ", search domains: ["
           << base::JoinString(properties.domain_search, ",") << "]";
  }
  if (!properties.dhcp_data.web_proxy_auto_discovery.empty()) {
    stream << ", wpad: " << properties.dhcp_data.web_proxy_auto_discovery;
  }
  if (properties.mtu != IPConfig::kUndefinedMTU) {
    stream << ", mtu: " << properties.mtu;
  }
  return stream << "}";
}

}  // namespace shill
