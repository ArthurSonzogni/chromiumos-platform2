// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/dhcpv4_config.h"

#include <optional>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_split.h>
#include <base/strings/stringprintf.h>
#include <base/time/time.h>
#include <chromeos/dbus/service_constants.h>
#include <net-base/http_url.h>
#include <net-base/ip_address.h>
#include <net-base/ipv4_address.h>

#include "shill/logging.h"

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kDHCP;
}  // namespace Logging

// static
bool DHCPv4Config::ParseClasslessStaticRoutes(
    const std::string& classless_routes,
    net_base::NetworkConfig* network_config) {
  if (classless_routes.empty()) {
    // It is not an error for this string to be empty.
    return true;
  }

  const auto route_strings = base::SplitString(
      classless_routes, " ", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
  if (route_strings.size() % 2) {
    LOG(ERROR) << "In " << __func__ << ": Size of route_strings array "
               << "is a non-even number: " << route_strings.size();
    return false;
  }

  std::vector<std::pair<net_base::IPv4CIDR, net_base::IPv4Address>> routes;
  auto route_iterator = route_strings.begin();
  // Classless routes are a space-delimited array of
  // "destination/prefix gateway" values.  As such, we iterate twice
  // for each pass of the loop below.
  while (route_iterator != route_strings.end()) {
    const auto& destination_as_string = *route_iterator;
    route_iterator++;

    const auto destination =
        net_base::IPv4CIDR::CreateFromCIDRString(destination_as_string);
    if (!destination) {
      LOG(ERROR) << "In " << __func__ << ": Expected an IP address/prefix "
                 << "but got an unparsable: " << destination_as_string;
      return false;
    }

    CHECK(route_iterator != route_strings.end());
    const auto& gateway_as_string = *route_iterator;
    route_iterator++;
    const auto gateway =
        net_base::IPv4Address::CreateFromString(gateway_as_string);
    if (!gateway) {
      LOG(ERROR) << "In " << __func__ << ": Expected a router IP address "
                 << "but got an unparsable: " << gateway_as_string;
      return false;
    }

    if (destination->prefix_length() == 0 && !network_config->ipv4_gateway) {
      // If a default route is provided in the classless parameters and
      // we don't already have one, apply this as the default route.
      SLOG(2) << "In " << __func__ << ": Setting default gateway to "
              << gateway_as_string;
      network_config->ipv4_gateway = gateway;
    } else {
      routes.emplace_back(std::make_pair(*destination, *gateway));
      SLOG(2) << "In " << __func__ << ": Adding route to to "
              << destination_as_string << " via " << gateway_as_string;
    }
  }

  network_config->rfc3442_routes.swap(routes);

  return true;
}

// static
bool DHCPv4Config::ParseConfiguration(const KeyValueStore& configuration,
                                      net_base::NetworkConfig* network_config,
                                      DHCPv4Config::Data* dhcp_data) {
  SLOG(2) << __func__;
  std::string classless_static_routes;
  bool default_gateway_parse_error = false;
  net_base::IPv4Address address;
  uint8_t prefix_length = 0;
  std::string domain_name;
  bool has_error = false;

  for (const auto& it : configuration.properties()) {
    const auto& key = it.first;
    const brillo::Any& value = it.second;
    SLOG(2) << "Processing key: " << key;
    if (key == kConfigurationKeyIPAddress) {
      address = net_base::IPv4Address(value.Get<uint32_t>());
      if (address.IsZero()) {
        LOG(ERROR) << "Invalid IP address.";
        has_error = true;
      }
    } else if (key == kConfigurationKeySubnetCIDR) {
      prefix_length = value.Get<uint8_t>();
    } else if (key == kConfigurationKeyBroadcastAddress) {
      network_config->ipv4_broadcast =
          net_base::IPv4Address(value.Get<uint32_t>());
      if (network_config->ipv4_broadcast->IsZero()) {
        LOG(ERROR) << "Ignoring invalid broadcast address.";
        network_config->ipv4_broadcast = std::nullopt;
        has_error = true;
      }
    } else if (key == kConfigurationKeyRouters) {
      const auto& routers = value.Get<std::vector<uint32_t>>();
      if (routers.empty()) {
        LOG(ERROR) << "No routers provided.";
        default_gateway_parse_error = true;
      } else {
        network_config->ipv4_gateway = net_base::IPv4Address(routers[0]);
        if (network_config->ipv4_gateway->IsZero()) {
          LOG(ERROR) << "Failed to parse router parameter provided.";
          network_config->ipv4_gateway = std::nullopt;
          default_gateway_parse_error = true;
        }
      }
    } else if (key == kConfigurationKeyDNS) {
      const auto& servers = value.Get<std::vector<uint32_t>>();
      for (auto it = servers.begin(); it != servers.end(); ++it) {
        auto server = net_base::IPAddress(net_base::IPv4Address(*it));
        if (server.IsZero()) {
          LOG(ERROR) << "Ignoring invalid DNS address.";
          has_error = true;
          continue;
        }
        network_config->dns_servers.push_back(std::move(server));
      }
    } else if (key == kConfigurationKeyDomainName) {
      domain_name = value.Get<std::string>();
    } else if (key == kConfigurationKeyDomainSearch) {
      network_config->dns_search_domains =
          value.Get<std::vector<std::string>>();
    } else if (key == kConfigurationKeyMTU) {
      int mtu = value.Get<uint16_t>();
      if (mtu > net_base::NetworkConfig::kMinIPv4MTU) {
        network_config->mtu = mtu;
      }
    } else if (key == kConfigurationKeyCaptivePortalUri) {
      // RFC 8910 specifies that the protocol of the URI must be HTTPS.
      const auto uri =
          net_base::HttpUrl::CreateFromString(value.Get<std::string>());
      if (!uri.has_value() ||
          uri->protocol() != net_base::HttpUrl::Protocol::kHttps) {
        LOG(ERROR) << "Ignoring invalid captive portal uri: "
                   << value.Get<std::string>();
        has_error = true;
        continue;
      }
      network_config->captive_portal_uri = *uri;
    } else if (key == kConfigurationKeyClasslessStaticRoutes) {
      classless_static_routes = value.Get<std::string>();
    } else if (key == kConfigurationKeyVendorEncapsulatedOptions) {
      dhcp_data->vendor_encapsulated_options = value.Get<ByteArray>();
    } else if (key == kConfigurationKeyWebProxyAutoDiscoveryUrl) {
      dhcp_data->web_proxy_auto_discovery = value.Get<std::string>();
    } else if (key == kConfigurationKeyLeaseTime) {
      dhcp_data->lease_duration = base::Seconds(value.Get<uint32_t>());
    } else if (key == kConfigurationKeyiSNSOptionData) {
      dhcp_data->isns_option_data = value.Get<ByteArray>();
    } else {
      SLOG(2) << "Key ignored.";
    }
  }
  if (!address.IsZero()) {
    if (prefix_length > 0) {
      network_config->ipv4_address =
          net_base::IPv4CIDR::CreateFromAddressAndPrefix(address,
                                                         prefix_length);
    }
    if (!network_config->ipv4_address) {
      LOG(ERROR) << "Invalid prefix length " << prefix_length
                 << ", ignoring address " << address;
      has_error = true;
    }
  }
  if (!domain_name.empty() && network_config->dns_search_domains.empty()) {
    network_config->dns_search_domains.push_back(domain_name + ".");
  }
  ParseClasslessStaticRoutes(classless_static_routes, network_config);
  return !has_error &&
         (!default_gateway_parse_error || network_config->ipv4_gateway);
}

}  // namespace shill
