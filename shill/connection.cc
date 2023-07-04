// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/connection.h"

#include <arpa/inet.h>
#include <linux/rtnetlink.h>
#include <unistd.h>

#include <utility>

#include <base/check.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/stl_util.h>
#include <base/strings/stringprintf.h>
#include <chromeos/dbus/shill/dbus-constants.h>
#include <net-base/ip_address.h>

#include "shill/logging.h"
#include "shill/net/ip_address.h"
#include "shill/network/address_service.h"
#include "shill/network/network_priority.h"
#include "shill/routing_table.h"
#include "shill/routing_table_entry.h"
#include "shill/technology.h"

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kConnection;
static std::string ObjectID(const Connection* c) {
  if (c == nullptr)
    return "(connection)";
  return c->interface_name();
}
}  // namespace Logging

Connection::Connection(int interface_index,
                       const std::string& interface_name,
                       bool fixed_ip_params,
                       Technology technology)
    : interface_index_(interface_index),
      interface_name_(interface_name),
      technology_(technology),
      fixed_ip_params_(fixed_ip_params),
      table_id_(RoutingTable::GetInterfaceTableId(interface_index)),
      local_(IPAddress::CreateFromFamily(IPAddress::kFamilyUnknown)),
      gateway_(IPAddress::CreateFromFamily(IPAddress::kFamilyUnknown)),
      routing_table_(RoutingTable::GetInstance()),
      address_service_(AddressService::GetInstance()) {
  SLOG(this, 2) << __func__ << "(" << interface_index << ", " << interface_name
                << ", " << technology << ")";
}

Connection::~Connection() {
  SLOG(this, 2) << __func__ << " " << interface_name_;

  routing_table_->FlushRoutes(interface_index_);
  routing_table_->FlushRoutesWithTag(interface_index_);
  if (!fixed_ip_params_) {
    address_service_->FlushAddress(interface_index_);
  }
}

bool Connection::SetupIncludedRoutes(const IPConfig::Properties& properties,
                                     bool ignore_gateway) {
  bool ret = true;

  IPAddress::Family address_family = properties.address_family;

  // Merge the routes to be installed from |dhcp_classless_static_routes| and
  // |inclusion_list|.
  std::vector<IPConfig::Route> included_routes =
      properties.dhcp_classless_static_routes;
  for (const auto& prefix_cidr : properties.inclusion_list) {
    const auto prefix =
        IPAddress::CreateFromPrefixString(prefix_cidr, address_family);
    if (!prefix.has_value()) {
      LOG(ERROR) << "Failed to parse prefix " << prefix_cidr;
      ret = false;
      continue;
    }
    IPConfig::Route route;
    prefix->IntoString(&route.host);
    route.prefix = prefix->prefix();
    route.gateway = properties.gateway;
    if (route.gateway.empty()) {
      // Gateway address with all-zeros indicates this route does not have a
      // gateway.
      route.gateway =
          (address_family == IPAddress::kFamilyIPv4) ? "0.0.0.0" : "::";
    }
    included_routes.push_back(route);
  }

  for (const auto& route : included_routes) {
    SLOG(this, 2) << "Installing route:"
                  << " Destination: " << route.host
                  << " Prefix: " << route.prefix
                  << " Gateway: " << route.gateway;
    const auto dst = IPAddress::CreateFromStringAndPrefix(
        route.host, route.prefix, address_family);
    if (!dst.has_value()) {
      LOG(ERROR) << "Failed to parse host " << route.host;
      ret = false;
      continue;
    }

    auto gateway = IPAddress::CreateFromString(route.gateway, address_family);
    if (!gateway.has_value()) {
      LOG(ERROR) << "Failed to parse gateway " << route.gateway;
      ret = false;
      continue;
    }
    if (ignore_gateway) {
      gateway->SetAddressToDefault();
    }

    // Left as default.
    const auto src = IPAddress::CreateFromFamily_Deprecated(address_family);

    if (!routing_table_->AddRoute(interface_index_,
                                  RoutingTableEntry::Create(*dst, src, *gateway)
                                      .SetTable(table_id_)
                                      .SetTag(interface_index_))) {
      ret = false;
    }
  }
  return ret;
}

bool Connection::SetupExcludedRoutes(const IPConfig::Properties& properties) {
  // If this connection has its own dedicated routing table, exclusion
  // is as simple as adding an RTN_THROW entry for each item on the list.
  // Traffic that matches the RTN_THROW entry will cause the kernel to
  // stop traversing our routing table and try the next rule in the list.
  IPAddress empty_ip =
      IPAddress::CreateFromFamily_Deprecated(properties.address_family);
  auto entry = RoutingTableEntry::Create(empty_ip, empty_ip, empty_ip)
                   .SetScope(RT_SCOPE_LINK)
                   .SetTable(table_id_)
                   .SetType(RTN_THROW)
                   .SetTag(interface_index_);
  for (const auto& excluded_ip : properties.exclusion_list) {
    auto dst = IPAddress::CreateFromPrefixString(excluded_ip,
                                                 properties.address_family);
    if (!dst.has_value()) {
      LOG(ERROR) << "Excluded prefix is invalid: " << excluded_ip;
      return false;
    }
    entry.dst = std::move(*dst);
    if (!routing_table_->AddRoute(interface_index_, entry)) {
      LOG(ERROR) << "Unable to setup route for " << excluded_ip;
      return false;
    }
  }
  return true;
}

void Connection::UpdateFromIPConfig(const IPConfig::Properties& properties) {
  SLOG(this, 2) << __func__ << " " << interface_name_;

  std::optional<IPAddress> gateway;
  if (!properties.gateway.empty()) {
    gateway = IPAddress::CreateFromString(properties.gateway);
    if (!gateway.has_value()) {
      LOG(ERROR) << "Gateway address " << properties.gateway << " is invalid";
      return;
    }
  }

  const auto local = IPAddress::CreateFromStringAndPrefix(
      properties.address, properties.subnet_prefix, properties.address_family);
  if (!local.has_value()) {
    LOG(ERROR) << "Local address " << properties.address << " is invalid";
    return;
  }

  std::optional<IPAddress> broadcast;
  if (properties.broadcast_address.empty()) {
    if (local->family() == IPAddress::kFamilyIPv4 &&
        properties.peer_address.empty()) {
      LOG(WARNING) << "Broadcast address is not set.  Using default.";
      broadcast = local->GetDefaultBroadcast();
    }
  } else {
    broadcast = IPAddress::CreateFromString(properties.broadcast_address,
                                            properties.address_family);
    if (!broadcast.has_value()) {
      LOG(ERROR) << "Broadcast address " << properties.broadcast_address
                 << " is invalid";
      return;
    }
  }

  bool is_p2p = false;
  if (!properties.peer_address.empty()) {
    const auto peer = IPAddress::CreateFromString(properties.peer_address,
                                                  properties.address_family);
    if (!peer.has_value()) {
      LOG(ERROR) << "Peer address " << properties.peer_address << " is invalid";
      return;
    }

    // For a PPP connection:
    // 1) Never set a peer (point-to-point) address, because the kernel
    //    will create an implicit routing rule in RT_TABLE_MAIN rather
    //    than our preferred routing table.  If the peer IP is set to the
    //    public IP of a VPN gateway (see below) this creates a routing loop.
    //    If not, it still creates an undesired route.
    // 2) Don't bother setting a gateway address either, because it doesn't
    //    have an effect on a point-to-point link.  So `ip route show table 1`
    //    will just say something like:
    //        default dev ppp0 metric 10
    is_p2p = true;
    // Reset |gateway| to default, so that the default route will be installed
    // by the code below.
    gateway = IPAddress::CreateFromFamily(properties.address_family);
  }

  auto local_cidr = *net_base::IPCIDR::CreateFromStringAndPrefix(
      properties.address, properties.subnet_prefix);
  auto broadcast_address =
      broadcast && local_cidr.GetFamily() == net_base::IPFamily::kIPv4
          ? net_base::IPv4Address::CreateFromString(
                properties.broadcast_address)
          : std::nullopt;
  // Skip address configuration if the address is from SLAAC. Note that IPv6
  // VPN uses kTypeVPN as method, so kTypeIPv6 is always SLAAC.
  const bool skip_ip_configuration = properties.method == kTypeIPv6;
  if (!fixed_ip_params_ && !skip_ip_configuration) {
    if (address_service_->RemoveAddressOtherThan(interface_index_,
                                                 local_cidr)) {
      // The address has changed for this interface.  We need to flush the
      // routes and start over.
      LOG(INFO) << __func__ << ": Flushing old addresses and routes.";
      // TODO(b/243336792): FlushRoutesWithTag() will not remove the IPv6
      // routes managed by the kernel so this will not cause any problem now.
      // Revisit this part later.
      routing_table_->FlushRoutesWithTag(interface_index_);
    }

    LOG(INFO) << __func__ << ": Installing with parameters:"
              << " interface_name=" << interface_name_
              << " local=" << local->ToString() << " broadcast="
              << (broadcast.has_value() ? broadcast->ToString() : "<empty>")
              << " gateway="
              << (gateway.has_value() ? gateway->ToString() : "<empty>");
    address_service_->AddAddress(interface_index_, local_cidr,
                                 broadcast_address);
  }

  if (!SetupExcludedRoutes(properties)) {
    return;
  }

  if (!is_p2p && !FixGatewayReachability(*local, gateway)) {
    LOG(WARNING) << "Expect limited network connectivity.";
  }

  // For VPNs IPv6 overlay shill has to create default route by itself.
  // For physical networks with RAs it is done by kernel.
  if (gateway.has_value() && properties.default_route) {
    const bool is_ipv4 = gateway->family() == IPAddress::kFamilyIPv4;
    const bool is_vpn_ipv6 = properties.method == kTypeVPN &&
                             gateway->family() == IPAddress::kFamilyIPv6;
    if (is_ipv4 || is_vpn_ipv6) {
      routing_table_->SetDefaultRoute(interface_index_, *gateway, table_id_);
    }
  }

  if (properties.blackhole_ipv6) {
    routing_table_->CreateBlackholeRoute(interface_index_,
                                         IPAddress::kFamilyIPv6, 0, table_id_);
  }

  if (!SetupIncludedRoutes(properties, /*ignore_gateway=*/is_p2p)) {
    LOG(WARNING) << "Failed to set up additional routes";
  }

  local_ = *local;
  if (gateway.has_value()) {
    gateway_ = *gateway;
  } else {
    gateway_ =
        IPAddress::CreateFromFamily_Deprecated(properties.address_family);
  }
}

bool Connection::FixGatewayReachability(
    const IPAddress& local, const std::optional<IPAddress>& gateway) {
  if (!gateway.has_value()) {
    LOG(WARNING) << "No gateway address was provided for this connection.";
    return false;
  }

  SLOG(2) << __func__ << " local " << local.ToString() << ", gateway "
          << gateway->ToString();

  // The prefix check will usually fail on IPv6 because IPv6 gateways
  // typically use link-local addresses.
  if (local.CanReachAddress(*gateway) ||
      local.family() == IPAddress::kFamilyIPv6) {
    return true;
  }

  LOG(WARNING) << "Gateway " << gateway->ToString()
               << " is unreachable from local address/prefix "
               << local.ToString() << "/" << local.prefix();
  LOG(WARNING) << "Mitigating this by creating a link route to the gateway.";

  IPAddress gateway_with_max_prefix(*gateway);
  gateway_with_max_prefix.set_prefix(
      IPAddress::GetMaxPrefixLength(gateway_with_max_prefix.family()));
  const auto default_address =
      IPAddress::CreateFromFamily_Deprecated(gateway->family());
  auto entry = RoutingTableEntry::Create(gateway_with_max_prefix,
                                         default_address, default_address)
                   .SetScope(RT_SCOPE_LINK)
                   .SetTable(table_id_)
                   .SetType(RTN_UNICAST)
                   .SetTag(interface_index_);

  if (!routing_table_->AddRoute(interface_index_, entry)) {
    LOG(ERROR) << "Unable to add link-scoped route to gateway.";
    return false;
  }

  return true;
}

bool Connection::IsIPv6() {
  return local_.family() == IPAddress::kFamilyIPv6;
}

}  // namespace shill
