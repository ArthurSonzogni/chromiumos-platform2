// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/connection.h"

#include <arpa/inet.h>
#include <linux/rtnetlink.h>
#include <unistd.h>

#include <limits>
#include <utility>

#include <base/bind.h>
#include <base/check.h>
#include <base/logging.h>
#include <base/stl_util.h>
#include <base/strings/stringprintf.h>

#include "shill/control_interface.h"
#include "shill/device_info.h"
#include "shill/logging.h"
#include "shill/manager.h"
#include "shill/net/rtnl_handler.h"
#include "shill/resolver.h"
#include "shill/routing_table.h"
#include "shill/routing_table_entry.h"

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kConnection;
static std::string ObjectID(const Connection* c) {
  if (c == nullptr)
    return "(connection)";
  return c->interface_name();
}
}  // namespace Logging

namespace {

// TODO(b/161507671) Use the constants defined in patchpanel::RoutingService at
// platform2/patchpanel/routing_service.cc after the routing layer is migrated
// to patchpanel.
constexpr const uint32_t kFwmarkRoutingMask = 0xffff0000;

RoutingPolicyEntry::FwMark GetFwmarkRoutingTag(int interface_index) {
  return {.value = RoutingTable::GetInterfaceTableId(interface_index) << 16,
          .mask = kFwmarkRoutingMask};
}

}  // namespace

// static
const uint32_t Connection::kDefaultPriority = 10;
// Allowed dsts rules are added right before the catchall rule. In this way,
// existing traffic from a different interface will not be "stolen" by these
// rules and sent out of the wrong interface, but the routes added to
// |table_id| will not be ignored.
const uint32_t Connection::kDstRulePriority =
    RoutingTable::kRulePriorityMain - 3;
const uint32_t Connection::kVpnUidRulePriority =
    RoutingTable::kRulePriorityMain - 2;
const uint32_t Connection::kCatchallPriority =
    RoutingTable::kRulePriorityMain - 1;
// UINT_MAX is also a valid priority, but we reserve this as a sentinel
// value, as in RoutingTable::GetDefaultRouteInternal.
const uint32_t Connection::kLeastPriority =
    std::numeric_limits<uint32_t>::max() - 1;
const uint32_t Connection::kPriorityStep = 10;

Connection::Connection(int interface_index,
                       const std::string& interface_name,
                       bool fixed_ip_params,
                       Technology technology,
                       const DeviceInfo* device_info)
    : use_dns_(false),
      priority_(kLeastPriority),
      is_primary_physical_(false),
      interface_index_(interface_index),
      interface_name_(interface_name),
      technology_(technology),
      use_if_addrs_(false),
      fixed_ip_params_(fixed_ip_params),
      table_id_(RoutingTable::GetInterfaceTableId(interface_index)),
      local_(IPAddress::kFamilyUnknown),
      gateway_(IPAddress::kFamilyUnknown),
      device_info_(device_info),
      resolver_(Resolver::GetInstance()),
      routing_table_(RoutingTable::GetInstance()),
      rtnl_handler_(RTNLHandler::GetInstance()) {
  SLOG(this, 2) << __func__ << "(" << interface_index << ", " << interface_name
                << ", " << technology << ")";
}

Connection::~Connection() {
  SLOG(this, 2) << __func__ << " " << interface_name_;

  routing_table_->FlushRoutes(interface_index_);
  routing_table_->FlushRoutesWithTag(interface_index_);
  if (!fixed_ip_params_) {
    device_info_->FlushAddresses(interface_index_, IPAddress::kFamilyUnknown);
  }
  routing_table_->FlushRules(interface_index_);
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
    IPAddress prefix(address_family);
    if (!prefix.SetAddressAndPrefixFromString(prefix_cidr)) {
      LOG(ERROR) << "Failed to parse prefix " << prefix_cidr;
      ret = false;
      continue;
    }
    IPConfig::Route route;
    prefix.IntoString(&route.host);
    route.prefix = prefix.prefix();
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
    IPAddress destination_address(address_family);
    IPAddress source_address(address_family);  // Left as default.
    IPAddress gateway_address(address_family);
    if (!destination_address.SetAddressFromString(route.host)) {
      LOG(ERROR) << "Failed to parse host " << route.host;
      ret = false;
      continue;
    }
    if (!gateway_address.SetAddressFromString(route.gateway)) {
      LOG(ERROR) << "Failed to parse gateway " << route.gateway;
      ret = false;
      continue;
    }
    if (ignore_gateway) {
      gateway_address.SetAddressToDefault();
    }
    destination_address.set_prefix(route.prefix);
    if (!routing_table_->AddRoute(
            interface_index_,
            RoutingTableEntry::Create(destination_address, source_address,
                                      gateway_address)
                .SetMetric(priority_)
                .SetTable(table_id_)
                .SetTag(interface_index_))) {
      ret = false;
    }
  }
  return ret;
}

bool Connection::SetupExcludedRoutes(const IPConfig::Properties& properties,
                                     const IPAddress& gateway) {
  // If this connection has its own dedicated routing table, exclusion
  // is as simple as adding an RTN_THROW entry for each item on the list.
  // Traffic that matches the RTN_THROW entry will cause the kernel to
  // stop traversing our routing table and try the next rule in the list.
  IPAddress empty_ip(properties.address_family);
  auto entry = RoutingTableEntry::Create(empty_ip, empty_ip, empty_ip)
                   .SetScope(RT_SCOPE_LINK)
                   .SetTable(table_id_)
                   .SetType(RTN_THROW)
                   .SetTag(interface_index_);
  for (const auto& excluded_ip : properties.exclusion_list) {
    if (!entry.dst.SetAddressAndPrefixFromString(excluded_ip) ||
        !entry.dst.IsValid() ||
        !routing_table_->AddRoute(interface_index_, entry)) {
      LOG(ERROR) << "Unable to setup route for " << excluded_ip << ".";
      return false;
    }
  }
  return true;
}

void Connection::UpdateFromIPConfig(const IPConfig::Properties& properties) {
  SLOG(this, 2) << __func__ << " " << interface_name_;

  allowed_dsts_.clear();
  for (const auto& route : properties.dhcp_classless_static_routes) {
    IPAddress dst(properties.address_family);
    if (!dst.SetAddressFromString(route.host)) {
      LOG(ERROR) << "Failed to parse static route address " << route.host;
      continue;
    }
    dst.set_prefix(route.prefix);
    allowed_dsts_.push_back(dst);
  }

  use_if_addrs_ =
      properties.use_if_addrs || IsPrimaryConnectivityTechnology(technology_);

  IPAddress gateway(properties.address_family);
  if (!properties.gateway.empty() &&
      !gateway.SetAddressFromString(properties.gateway)) {
    LOG(ERROR) << "Gateway address " << properties.gateway << " is invalid";
    return;
  }

  IPAddress local(properties.address_family);
  if (!local.SetAddressFromString(properties.address)) {
    LOG(ERROR) << "Local address " << properties.address << " is invalid";
    return;
  }
  local.set_prefix(properties.subnet_prefix);

  IPAddress broadcast(properties.address_family);
  if (properties.broadcast_address.empty()) {
    if (local.family() == IPAddress::kFamilyIPv4 &&
        properties.peer_address.empty()) {
      LOG(WARNING) << "Broadcast address is not set.  Using default.";
      broadcast = local.GetDefaultBroadcast();
    }
  } else if (!broadcast.SetAddressFromString(properties.broadcast_address)) {
    LOG(ERROR) << "Broadcast address " << properties.broadcast_address
               << " is invalid";
    return;
  }

  IPAddress peer(properties.address_family);
  if (!properties.peer_address.empty() &&
      !peer.SetAddressFromString(properties.peer_address)) {
    LOG(ERROR) << "Peer address " << properties.peer_address << " is invalid";
    return;
  }
  bool is_p2p = peer.IsValid();
  if (is_p2p) {
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
    peer.SetAddressToDefault();
    gateway.SetAddressToDefault();
  }

  if (!fixed_ip_params_) {
    if (device_info_->HasOtherAddress(interface_index_, local)) {
      // The address has changed for this interface.  We need to flush
      // everything and start over.
      LOG(INFO) << __func__ << ": Flushing old addresses and routes.";
      // TODO(b/243336792): FlushRoutesWithTag() will not remove the IPv6 routes
      // managed by the kernel so this will not cause any problem now. Revisit
      // this part later.
      routing_table_->FlushRoutesWithTag(interface_index_);
      device_info_->FlushAddresses(interface_index_, local.family());
    }

    LOG(INFO) << __func__ << ": Installing with parameters:"
              << " interface_name=" << interface_name_
              << " local=" << local.ToString()
              << " broadcast=" << broadcast.ToString()
              << " peer=" << peer.ToString()
              << " gateway=" << gateway.ToString();

    rtnl_handler_->AddInterfaceAddress(interface_index_, local, broadcast,
                                       peer);
    SetMTU(properties.mtu);
  }

  if (!SetupExcludedRoutes(properties, gateway)) {
    return;
  }

  if (!is_p2p && !FixGatewayReachability(local, gateway)) {
    LOG(WARNING) << "Expect limited network connectivity.";
  }

  if (gateway.IsValid() && properties.default_route &&
      gateway.family() == IPAddress::kFamilyIPv4) {
    // For IPv6 we rely on default route added by kernel
    routing_table_->SetDefaultRoute(interface_index_, gateway, table_id_);
  }

  if (properties.blackhole_ipv6) {
    routing_table_->CreateBlackholeRoute(interface_index_,
                                         IPAddress::kFamilyIPv6, 0, table_id_);
  }

  if (!SetupIncludedRoutes(properties, /*ignore_gateway =*/is_p2p)) {
    LOG(WARNING) << "Failed to set up additional routes";
  }

  UpdateRoutingPolicy();

  // Save a copy of the last non-null DNS config.
  if (!properties.dns_servers.empty()) {
    dns_servers_ = properties.dns_servers;
  }

  if (!properties.domain_search.empty()) {
    dns_domain_search_ = properties.domain_search;
  }

  if (!properties.domain_name.empty()) {
    dns_domain_name_ = properties.domain_name;
  }

  PushDNSConfig();

  local_ = local;
  gateway_ = gateway;
}

void Connection::UpdateRoutingPolicy() {
  routing_table_->FlushRules(interface_index_);

  // b/180521518: IPv6 routing rules are always omitted for a Cellular
  // connection that is not the primary physical connection. This prevents
  // applications from accidentally using the Cellular network and causing data
  // charges with IPv6 traffic when the primary physical connection is IPv4
  // only.
  bool no_ipv6 = technology_ == Technology::kCellular && !is_primary_physical_;

  AllowTrafficThrough(table_id_, priority_, no_ipv6);

  // b/177620923 Add uid rules just before the default rule to route to the VPN
  // interface any untagged traffic owner by a uid routed through VPN
  // connections. These rules are necessary for consistency between source IP
  // address selection algorithm that ignores iptables fwmark tagging rules, and
  // the actual routing of packets that have been tagged in iptables PREROUTING.
  if (technology_ == Technology::kVPN) {
    for (const auto& uid : routing_table_->GetUserTrafficUids()) {
      auto entry = RoutingPolicyEntry::Create(IPAddress::kFamilyIPv4)
                       .SetPriority(kVpnUidRulePriority)
                       .SetTable(table_id_)
                       .SetUid(uid);
      routing_table_->AddRule(interface_index_, entry);
      routing_table_->AddRule(interface_index_, entry.FlipFamily());
    }
  }

  if (use_if_addrs_ && is_primary_physical_) {
    // Main routing table contains kernel-added routes for source address
    // selection. Sending traffic there before all other rules for physical
    // interfaces (but after any VPN rules) ensures that physical interface
    // rules are not inadvertently too aggressive.
    auto main_table_rule =
        RoutingPolicyEntry::CreateFromSrc(IPAddress(IPAddress::kFamilyIPv4))
            .SetPriority(priority_ - 1)
            .SetTable(RT_TABLE_MAIN);
    routing_table_->AddRule(interface_index_, main_table_rule);
    routing_table_->AddRule(interface_index_, main_table_rule.FlipFamily());
    // Add a default routing rule to use the primary interface if there is
    // nothing better.
    // TODO(crbug.com/999589) Remove this rule.
    auto catch_all_rule =
        RoutingPolicyEntry::CreateFromSrc(IPAddress(IPAddress::kFamilyIPv4))
            .SetTable(table_id_)
            .SetPriority(kCatchallPriority);
    routing_table_->AddRule(interface_index_, catch_all_rule);
    routing_table_->AddRule(interface_index_, catch_all_rule.FlipFamily());
  }
}

void Connection::AllowTrafficThrough(uint32_t table_id,
                                     uint32_t base_priority,
                                     bool no_ipv6) {
  // b/189952150: when |no_ipv6| is true and shill must prevent IPv6 traffic on
  // this connection for applications, it is still necessary to ensure that some
  // critical system IPv6 traffic can be routed. Example: shill portal detection
  // probes when the network connection is IPv6 only. For the time being the
  // only supported case is traffic from shill.
  uint32_t shill_uid = getuid();

  for (const auto& dst_address : allowed_dsts_) {
    auto dst_addr_rule = RoutingPolicyEntry::CreateFromDst(dst_address)
                             .SetPriority(kDstRulePriority)
                             .SetTable(table_id);
    if (dst_address.family() == IPAddress::kFamilyIPv6 && no_ipv6) {
      dst_addr_rule.SetUid(shill_uid);
    }
    routing_table_->AddRule(interface_index_, dst_addr_rule);
  }

  // Always set a rule for matching traffic tagged with the fwmark routing tag
  // corresponding to this network interface.
  auto fwmark_routing_entry =
      RoutingPolicyEntry::Create(IPAddress::kFamilyIPv4)
          .SetPriority(base_priority)
          .SetTable(table_id)
          .SetFwMark(GetFwmarkRoutingTag(interface_index_));
  routing_table_->AddRule(interface_index_, fwmark_routing_entry);
  if (no_ipv6) {
    fwmark_routing_entry.SetUid(shill_uid);
  }
  routing_table_->AddRule(interface_index_, fwmark_routing_entry.FlipFamily());

  // Add output interface rule for all interfaces, such that SO_BINDTODEVICE can
  // be used without explicitly binding the socket.
  auto oif_rule =
      RoutingPolicyEntry::CreateFromSrc(IPAddress(IPAddress::kFamilyIPv4))
          .SetTable(table_id)
          .SetPriority(base_priority)
          .SetOif(interface_name_);
  routing_table_->AddRule(interface_index_, oif_rule);
  if (no_ipv6) {
    oif_rule.SetUid(shill_uid);
  }
  routing_table_->AddRule(interface_index_, oif_rule.FlipFamily());

  if (use_if_addrs_) {
    // Select the per-device table if the outgoing packet's src address matches
    // the interface's addresses or the input interface is this interface.
    //
    // TODO(crbug.com/941597) This may need to change when NDProxy allows guests
    // to provision IPv6 addresses.
    for (const auto& address : device_info_->GetAddresses(interface_index_)) {
      auto if_addr_rule = RoutingPolicyEntry::CreateFromSrc(address)
                              .SetTable(table_id)
                              .SetPriority(base_priority);
      if (address.family() == IPAddress::kFamilyIPv6 && no_ipv6) {
        if_addr_rule.SetUid(shill_uid);
      }
      routing_table_->AddRule(interface_index_, if_addr_rule);
    }
    auto iif_rule =
        RoutingPolicyEntry::CreateFromSrc(IPAddress(IPAddress::kFamilyIPv4))
            .SetTable(table_id)
            .SetPriority(base_priority)
            .SetIif(interface_name_);
    routing_table_->AddRule(interface_index_, iif_rule);
    if (no_ipv6) {
      iif_rule.SetUid(shill_uid);
    }
    routing_table_->AddRule(interface_index_, iif_rule.FlipFamily());
  }
}

void Connection::SetPriority(uint32_t priority, bool is_primary_physical) {
  SLOG(this, 2) << __func__ << " " << interface_name_ << " (index "
                << interface_index_ << ")" << priority_ << " -> " << priority;
  if (priority == priority_) {
    return;
  }

  priority_ = priority;
  is_primary_physical_ = is_primary_physical;
  UpdateRoutingPolicy();

  PushDNSConfig();
  routing_table_->FlushCache();
}

bool Connection::IsDefault() const {
  return priority_ == kDefaultPriority;
}

void Connection::SetUseDNS(bool enable) {
  SLOG(this, 2) << __func__ << " " << interface_name_ << " (index "
                << interface_index_ << ")" << use_dns_ << " -> " << enable;
  use_dns_ = enable;
}

void Connection::UpdateDNSServers(const std::vector<std::string>& dns_servers) {
  dns_servers_ = dns_servers;
  PushDNSConfig();
}

void Connection::PushDNSConfig() {
  if (!use_dns_) {
    return;
  }

  auto domain_search = dns_domain_search_;
  if (domain_search.empty() && !dns_domain_name_.empty()) {
    SLOG(this, 2) << "Setting domain search to domain name "
                  << dns_domain_name_;
    domain_search.push_back(dns_domain_name_ + ".");
  }
  resolver_->SetDNSFromLists(dns_servers_, domain_search);
}

bool Connection::FixGatewayReachability(const IPAddress& local,
                                        const IPAddress& gateway) {
  SLOG(2) << __func__ << " local " << local.ToString() << ", gateway "
          << gateway.ToString();

  if (!gateway.IsValid()) {
    LOG(WARNING) << "No gateway address was provided for this connection.";
    return false;
  }

  // The prefix check will usually fail on IPv6 because IPv6 gateways
  // typically use link-local addresses.
  if (local.CanReachAddress(gateway) ||
      local.family() == IPAddress::kFamilyIPv6) {
    return true;
  }

  LOG(WARNING) << "Gateway " << gateway.ToString()
               << " is unreachable from local address/prefix "
               << local.ToString() << "/" << local.prefix();
  LOG(WARNING) << "Mitigating this by creating a link route to the gateway.";

  IPAddress gateway_with_max_prefix(gateway);
  gateway_with_max_prefix.set_prefix(
      IPAddress::GetMaxPrefixLength(gateway_with_max_prefix.family()));
  IPAddress default_address(gateway.family());
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

void Connection::SetMTU(int32_t mtu) {
  SLOG(this, 2) << __func__ << " " << mtu;
  // Make sure the MTU value is valid.
  if (mtu == IPConfig::kUndefinedMTU) {
    mtu = IPConfig::kDefaultMTU;
  } else {
    int min_mtu = IsIPv6() ? IPConfig::kMinIPv6MTU : IPConfig::kMinIPv4MTU;
    if (mtu < min_mtu) {
      SLOG(this, 2) << __func__ << " MTU " << mtu
                    << " is too small; adjusting up to " << min_mtu;
      mtu = min_mtu;
    }
  }

  rtnl_handler_->SetInterfaceMTU(interface_index_, mtu);
}

bool Connection::IsIPv6() {
  return local_.family() == IPAddress::kFamilyIPv6;
}

}  // namespace shill
