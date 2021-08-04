// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/dhcp/dhcpv4_config.h"

#include <arpa/inet.h>

#include <base/check.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/stl_util.h>
#include <base/strings/string_split.h>
#include <base/strings/stringprintf.h>
#include <chromeos/dbus/service_constants.h>

#include "shill/dhcp/dhcp_provider.h"
#include "shill/logging.h"
#include "shill/metrics.h"
#include "shill/net/ip_address.h"

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kDHCP;
static std::string ObjectID(const DHCPv4Config* d) {
  if (d == nullptr)
    return "(DHCPv4_config)";
  else
    return d->device_name();
}
}  // namespace Logging

// static
const char DHCPv4Config::kDHCPCDPathFormatPID[] =
    "var/run/dhcpcd/dhcpcd-%s-4.pid";
const char DHCPv4Config::kConfigurationKeyBroadcastAddress[] =
    "BroadcastAddress";
const char DHCPv4Config::kConfigurationKeyClasslessStaticRoutes[] =
    "ClasslessStaticRoutes";
const char DHCPv4Config::kConfigurationKeyDNS[] = "DomainNameServers";
const char DHCPv4Config::kConfigurationKeyDomainName[] = "DomainName";
const char DHCPv4Config::kConfigurationKeyDomainSearch[] = "DomainSearch";
const char DHCPv4Config::kConfigurationKeyHostname[] = "Hostname";
const char DHCPv4Config::kConfigurationKeyIPAddress[] = "IPAddress";
const char DHCPv4Config::kConfigurationKeyiSNSOptionData[] = "iSNSOptionData";
const char DHCPv4Config::kConfigurationKeyLeaseTime[] = "DHCPLeaseTime";
const char DHCPv4Config::kConfigurationKeyMTU[] = "InterfaceMTU";
const char DHCPv4Config::kConfigurationKeyRouters[] = "Routers";
const char DHCPv4Config::kConfigurationKeySubnetCIDR[] = "SubnetCIDR";
const char DHCPv4Config::kConfigurationKeyVendorEncapsulatedOptions[] =
    "VendorEncapsulatedOptions";
const char DHCPv4Config::kConfigurationKeyWebProxyAutoDiscoveryUrl[] =
    "WebProxyAutoDiscoveryUrl";
const char DHCPv4Config::kReasonBound[] = "BOUND";
const char DHCPv4Config::kReasonFail[] = "FAIL";
const char DHCPv4Config::kReasonGatewayArp[] = "GATEWAY-ARP";
const char DHCPv4Config::kReasonNak[] = "NAK";
const char DHCPv4Config::kReasonRebind[] = "REBIND";
const char DHCPv4Config::kReasonReboot[] = "REBOOT";
const char DHCPv4Config::kReasonRenew[] = "RENEW";
const char DHCPv4Config::kStatusArpGateway[] = "ArpGateway";
const char DHCPv4Config::kStatusArpSelf[] = "ArpSelf";
const char DHCPv4Config::kStatusBound[] = "Bound";
const char DHCPv4Config::kStatusDiscover[] = "Discover";
const char DHCPv4Config::kStatusIgnoreAdditionalOffer[] =
    "IgnoreAdditionalOffer";
const char DHCPv4Config::kStatusIgnoreFailedOffer[] = "IgnoreFailedOffer";
const char DHCPv4Config::kStatusIgnoreInvalidOffer[] = "IgnoreInvalidOffer";
const char DHCPv4Config::kStatusIgnoreNonOffer[] = "IgnoreNonOffer";
const char DHCPv4Config::kStatusInform[] = "Inform";
const char DHCPv4Config::kStatusInit[] = "Init";
const char DHCPv4Config::kStatusNakDefer[] = "NakDefer";
const char DHCPv4Config::kStatusRebind[] = "Rebind";
const char DHCPv4Config::kStatusReboot[] = "Reboot";
const char DHCPv4Config::kStatusRelease[] = "Release";
const char DHCPv4Config::kStatusRenew[] = "Renew";
const char DHCPv4Config::kStatusRequest[] = "Request";
const char DHCPv4Config::kType[] = "dhcp";

DHCPv4Config::DHCPv4Config(ControlInterface* control_interface,
                           EventDispatcher* dispatcher,
                           DHCPProvider* provider,
                           const std::string& device_name,
                           const std::string& lease_file_suffix,
                           bool arp_gateway,
                           const DhcpProperties& dhcp_props,
                           Metrics* metrics)
    : DHCPConfig(control_interface,
                 dispatcher,
                 provider,
                 device_name,
                 kType,
                 lease_file_suffix),
      arp_gateway_(arp_gateway),
      is_gateway_arp_active_(false),
      metrics_(metrics) {
  dhcp_props.GetValueForProperty(DhcpProperties::kHostnameProperty, &hostname_);
  dhcp_props.GetValueForProperty(DhcpProperties::kVendorClassProperty,
                                 &vendor_class_);
  SLOG(this, 2) << __func__ << ": " << device_name;
}

DHCPv4Config::~DHCPv4Config() {
  SLOG(this, 2) << __func__ << ": " << device_name();
}

void DHCPv4Config::ProcessEventSignal(const std::string& reason,
                                      const KeyValueStore& configuration) {
  LOG(INFO) << "Event reason: " << reason;
  if (reason == kReasonFail) {
    LOG(ERROR) << "Received failure event from DHCP client.";
    NotifyFailure();
    return;
  } else if (reason == kReasonNak) {
    // If we got a NAK, this means the DHCP server is active, and any
    // Gateway ARP state we have is no longer sufficient.
    LOG_IF(ERROR, is_gateway_arp_active_)
        << "Received NAK event for our gateway-ARP lease.";
    is_gateway_arp_active_ = false;
    return;
  } else if (reason != kReasonBound && reason != kReasonRebind &&
             reason != kReasonReboot && reason != kReasonRenew &&
             reason != kReasonGatewayArp) {
    LOG(WARNING) << "Event ignored.";
    return;
  }
  IPConfig::Properties properties;
  CHECK(ParseConfiguration(configuration, &properties));

  // This needs to be set before calling UpdateProperties() below since
  // those functions may indirectly call other methods like ReleaseIP that
  // depend on or change this value.
  set_is_lease_active(true);

  if (reason == kReasonGatewayArp) {
    // This is a non-authoritative confirmation that we or on the same
    // network as the one we received a lease on previously.  The DHCP
    // client is still running, so we should not cancel the timeout
    // until that completes.  In the meantime, however, we can tentatively
    // configure our network in anticipation of successful completion.
    IPConfig::UpdateProperties(properties, false);
    is_gateway_arp_active_ = true;
  } else {
    DHCPConfig::UpdateProperties(properties, true);
    is_gateway_arp_active_ = false;
  }
}

void DHCPv4Config::ProcessStatusChangeSignal(const std::string& status) {
  SLOG(this, 2) << __func__ << ": " << status;

  if (status == kStatusArpGateway) {
    metrics_->NotifyDhcpClientStatus(Metrics::kDhcpClientStatusArpGateway);
  } else if (status == kStatusArpSelf) {
    metrics_->NotifyDhcpClientStatus(Metrics::kDhcpClientStatusArpSelf);
  } else if (status == kStatusBound) {
    metrics_->NotifyDhcpClientStatus(Metrics::kDhcpClientStatusBound);
  } else if (status == kStatusDiscover) {
    metrics_->NotifyDhcpClientStatus(Metrics::kDhcpClientStatusDiscover);
  } else if (status == kStatusIgnoreAdditionalOffer) {
    metrics_->NotifyDhcpClientStatus(
        Metrics::kDhcpClientStatusIgnoreAdditionalOffer);
  } else if (status == kStatusIgnoreFailedOffer) {
    metrics_->NotifyDhcpClientStatus(
        Metrics::kDhcpClientStatusIgnoreFailedOffer);
  } else if (status == kStatusIgnoreInvalidOffer) {
    metrics_->NotifyDhcpClientStatus(
        Metrics::kDhcpClientStatusIgnoreInvalidOffer);
  } else if (status == kStatusIgnoreNonOffer) {
    metrics_->NotifyDhcpClientStatus(Metrics::kDhcpClientStatusIgnoreNonOffer);
  } else if (status == kStatusInform) {
    metrics_->NotifyDhcpClientStatus(Metrics::kDhcpClientStatusInform);
  } else if (status == kStatusInit) {
    metrics_->NotifyDhcpClientStatus(Metrics::kDhcpClientStatusInit);
  } else if (status == kStatusNakDefer) {
    metrics_->NotifyDhcpClientStatus(Metrics::kDhcpClientStatusNakDefer);
  } else if (status == kStatusRebind) {
    metrics_->NotifyDhcpClientStatus(Metrics::kDhcpClientStatusRebind);
  } else if (status == kStatusReboot) {
    metrics_->NotifyDhcpClientStatus(Metrics::kDhcpClientStatusReboot);
  } else if (status == kStatusRelease) {
    metrics_->NotifyDhcpClientStatus(Metrics::kDhcpClientStatusRelease);
  } else if (status == kStatusRenew) {
    metrics_->NotifyDhcpClientStatus(Metrics::kDhcpClientStatusRenew);
  } else if (status == kStatusRequest) {
    metrics_->NotifyDhcpClientStatus(Metrics::kDhcpClientStatusRequest);
  } else {
    LOG(ERROR) << "DHCP client reports unknown status " << status;
  }
}

void DHCPv4Config::CleanupClientState() {
  DHCPConfig::CleanupClientState();

  // Delete lease file if it is ephemeral.
  if (IsEphemeralLease()) {
    base::DeleteFile(root().Append(base::StringPrintf(
        DHCPProvider::kDHCPCDPathFormatLease, device_name().c_str())));
  }
  base::DeleteFile(root().Append(
      base::StringPrintf(kDHCPCDPathFormatPID, device_name().c_str())));
  is_gateway_arp_active_ = false;
}

bool DHCPv4Config::ShouldFailOnAcquisitionTimeout() {
  // Continue to use previous lease if gateway ARP is active.
  return !is_gateway_arp_active_;
}

bool DHCPv4Config::ShouldKeepLeaseOnDisconnect() {
  // If we are using gateway unicast ARP to speed up re-connect, don't
  // give up our leases when we disconnect.
  return arp_gateway_;
}

std::vector<std::string> DHCPv4Config::GetFlags() {
  // Get default flags first.
  auto flags = DHCPConfig::GetFlags();

  flags.push_back("-4");  // IPv4 only.

  // Apply options from DhcpProperties when applicable.
  if (!hostname_.empty()) {
    flags.push_back("-h");  // Request hostname from server
    flags.push_back(hostname_);
  }
  if (!vendor_class_.empty()) {
    flags.push_back("-i");
    flags.push_back(vendor_class_);
  }

  if (arp_gateway_) {
    flags.push_back("-R");  // ARP for default gateway.
    flags.push_back("-P");  // Enable unicast ARP on renew.
  }
  return flags;
}

// static
std::string DHCPv4Config::GetIPv4AddressString(unsigned int address) {
  char str[INET_ADDRSTRLEN];
  if (inet_ntop(AF_INET, &address, str, base::size(str))) {
    return str;
  }
  LOG(ERROR) << "Unable to convert IPv4 address to string: " << address;
  return "";
}

// static
bool DHCPv4Config::ParseClasslessStaticRoutes(
    const std::string& classless_routes, IPConfig::Properties* properties) {
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

  std::vector<IPConfig::Route> routes;
  std::vector<IPAddress> destinations;
  auto route_iterator = route_strings.begin();
  // Classless routes are a space-delimited array of
  // "destination/prefix gateway" values.  As such, we iterate twice
  // for each pass of the loop below.
  while (route_iterator != route_strings.end()) {
    const auto& destination_as_string = *route_iterator;
    route_iterator++;
    IPAddress destination(IPAddress::kFamilyIPv4);
    if (!destination.SetAddressAndPrefixFromString(destination_as_string)) {
      LOG(ERROR) << "In " << __func__ << ": Expected an IP address/prefix "
                 << "but got an unparsable: " << destination_as_string;
      return false;
    }

    CHECK(route_iterator != route_strings.end());
    const auto& gateway_as_string = *route_iterator;
    route_iterator++;
    IPAddress gateway(IPAddress::kFamilyIPv4);
    if (!gateway.SetAddressFromString(gateway_as_string)) {
      LOG(ERROR) << "In " << __func__ << ": Expected a router IP address "
                 << "but got an unparsable: " << gateway_as_string;
      return false;
    }

    if (destination.prefix() == 0 && properties->gateway.empty()) {
      // If a default route is provided in the classless parameters and
      // we don't already have one, apply this as the default route.
      SLOG(nullptr, 2) << "In " << __func__ << ": Setting default gateway to "
                       << gateway_as_string;
      CHECK(gateway.IntoString(&properties->gateway));
    } else {
      IPConfig::Route route;
      CHECK(destination.IntoString(&route.host));
      route.prefix = destination.prefix();
      CHECK(gateway.IntoString(&route.gateway));
      routes.push_back(route);
      destinations.push_back(destination);
      SLOG(nullptr, 2) << "In " << __func__ << ": Adding route to to "
                       << destination_as_string << " via " << gateway_as_string;
    }
  }

  if (!routes.empty()) {
    properties->routes.swap(routes);
    properties->included_dsts.swap(destinations);
  }

  return true;
}

// static
bool DHCPv4Config::ParseConfiguration(const KeyValueStore& configuration,
                                      IPConfig::Properties* properties) {
  SLOG(nullptr, 2) << __func__;
  properties->method = kTypeDHCP;
  properties->address_family = IPAddress::kFamilyIPv4;
  std::string classless_static_routes;
  bool default_gateway_parse_error = false;
  for (const auto& it : configuration.properties()) {
    const auto& key = it.first;
    const brillo::Any& value = it.second;
    SLOG(nullptr, 2) << "Processing key: " << key;
    if (key == kConfigurationKeyIPAddress) {
      properties->address = GetIPv4AddressString(value.Get<uint32_t>());
      if (properties->address.empty()) {
        return false;
      }
    } else if (key == kConfigurationKeySubnetCIDR) {
      properties->subnet_prefix = value.Get<uint8_t>();
    } else if (key == kConfigurationKeyBroadcastAddress) {
      properties->broadcast_address =
          GetIPv4AddressString(value.Get<uint32_t>());
      if (properties->broadcast_address.empty()) {
        return false;
      }
    } else if (key == kConfigurationKeyRouters) {
      const auto& routers = value.Get<std::vector<uint32_t>>();
      if (routers.empty()) {
        LOG(ERROR) << "No routers provided.";
        default_gateway_parse_error = true;
      } else {
        properties->gateway = GetIPv4AddressString(routers[0]);
        if (properties->gateway.empty()) {
          LOG(ERROR) << "Failed to parse router parameter provided.";
          default_gateway_parse_error = true;
        }
      }
    } else if (key == kConfigurationKeyDNS) {
      const auto& servers = value.Get<std::vector<uint32_t>>();
      for (auto it = servers.begin(); it != servers.end(); ++it) {
        std::string server = GetIPv4AddressString(*it);
        if (server.empty()) {
          return false;
        }
        properties->dns_servers.push_back(std::move(server));
      }
    } else if (key == kConfigurationKeyDomainName) {
      properties->domain_name = value.Get<std::string>();
    } else if (key == kConfigurationKeyHostname) {
      properties->accepted_hostname = value.Get<std::string>();
    } else if (key == kConfigurationKeyDomainSearch) {
      properties->domain_search = value.Get<std::vector<std::string>>();
    } else if (key == kConfigurationKeyMTU) {
      int mtu = value.Get<uint16_t>();
      metrics_->SendSparseToUMA(Metrics::kMetricDhcpClientMTUValue, mtu);
      if (mtu >= minimum_mtu() && mtu != kMinIPv4MTU) {
        properties->mtu = mtu;
      }
    } else if (key == kConfigurationKeyClasslessStaticRoutes) {
      classless_static_routes = value.Get<std::string>();
    } else if (key == kConfigurationKeyVendorEncapsulatedOptions) {
      properties->vendor_encapsulated_options = value.Get<ByteArray>();
    } else if (key == kConfigurationKeyWebProxyAutoDiscoveryUrl) {
      properties->web_proxy_auto_discovery = value.Get<std::string>();
    } else if (key == kConfigurationKeyLeaseTime) {
      properties->lease_duration_seconds = value.Get<uint32_t>();
    } else if (key == kConfigurationKeyiSNSOptionData) {
      properties->isns_option_data = value.Get<ByteArray>();
    } else {
      SLOG(nullptr, 2) << "Key ignored.";
    }
  }
  ParseClasslessStaticRoutes(classless_static_routes, properties);
  if (default_gateway_parse_error && properties->gateway.empty()) {
    return false;
  }
  return true;
}

}  // namespace shill
