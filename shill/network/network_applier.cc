// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/network_applier.h"

#include <linux/fib_rules.h>

#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <base/memory/ptr_util.h>
#include <net-base/ip_address.h>

#include "shill/ipconfig.h"
#include "shill/net/ip_address.h"
#include "shill/net/rtnl_handler.h"
#include "shill/network/address_service.h"
#include "shill/network/network_priority.h"
#include "shill/routing_policy_service.h"
#include "shill/routing_table.h"
#include "shill/routing_table_entry.h"

namespace shill {

namespace {
// TODO(b/161507671) Use the constants defined in patchpanel::RoutingService at
// platform2/patchpanel/routing_service.cc after the routing layer is migrated
// to patchpanel.
constexpr const uint32_t kFwmarkRoutingMask = 0xffff0000;

RoutingPolicyEntry::FwMark GetFwmarkRoutingTag(int interface_index) {
  return {.value = RoutingTable::GetInterfaceTableId(interface_index) << 16,
          .mask = kFwmarkRoutingMask};
}

// The routing rule priority used for the default service, whether physical or
// VPN.
constexpr uint32_t kDefaultPriority = 10;
// Space between the priorities of services. The Nth highest priority service
// (starting from N=0) will have a rule priority of
// |kDefaultPriority| + N*|kPriorityStep|.
constexpr uint32_t kPriorityStep = 10;
// An offset added to the priority of non-VPN services, so their rules comes
// after the main table rule.
constexpr uint32_t kPhysicalPriorityOffset = 1000;
// Priority for rules corresponding to IPConfig::Properties::routes.
// Allowed dsts rules are added right before the catchall rule. In this way,
// existing traffic from a different interface will not be "stolen" by these
// rules and sent out of the wrong interface, but the routes added to
// |table_id| will not be ignored.
constexpr uint32_t kDstRulePriority =
    RoutingPolicyService::kRulePriorityMain - 3;
// Priority for VPN rules routing traffic or specific uids with the routing
// table of a VPN connection.
constexpr uint32_t kVpnUidRulePriority =
    RoutingPolicyService::kRulePriorityMain - 2;
// Priority for the rule sending any remaining traffic to the default physical
// interface.
constexpr uint32_t kCatchallPriority =
    RoutingPolicyService::kRulePriorityMain - 1;
}  // namespace

NetworkApplier::NetworkApplier()
    : resolver_(Resolver::GetInstance()),
      rule_table_(RoutingPolicyService::GetInstance()),
      routing_table_(RoutingTable::GetInstance()),
      address_service_(AddressService::GetInstance()),
      rtnl_handler_(RTNLHandler::GetInstance()),
      proc_fs_(std::make_unique<ProcFsStub>("")) {}

NetworkApplier::~NetworkApplier() = default;

// static
NetworkApplier* NetworkApplier::GetInstance() {
  static base::NoDestructor<NetworkApplier> instance;
  return instance.get();
}

// static
std::unique_ptr<NetworkApplier> NetworkApplier::CreateForTesting(
    Resolver* resolver,
    RoutingTable* routing_table,
    RoutingPolicyService* rule_table,
    AddressService* address_service,
    RTNLHandler* rtnl_handler,
    std::unique_ptr<ProcFsStub> proc_fs) {
  // Using `new` to access a non-public constructor.
  auto ptr = base::WrapUnique(new NetworkApplier());
  ptr->resolver_ = resolver;
  ptr->routing_table_ = routing_table;
  ptr->rule_table_ = rule_table;
  ptr->address_service_ = address_service;
  ptr->rtnl_handler_ = rtnl_handler;
  ptr->proc_fs_ = std::move(proc_fs);
  return ptr;
}

void NetworkApplier::Clear(int interface_index) {
  rule_table_->FlushRules(interface_index);
  routing_table_->FlushRoutes(interface_index);
  routing_table_->FlushRoutesWithTag(interface_index,
                                     net_base::IPFamily::kIPv4);
  routing_table_->FlushRoutesWithTag(interface_index,
                                     net_base::IPFamily::kIPv6);
  address_service_->FlushAddress(interface_index);
  proc_fs_->FlushRoutingCache();
}

void NetworkApplier::ApplyDNS(NetworkPriority priority,
                              const IPConfig::Properties* ipv4_properties,
                              const IPConfig::Properties* ipv6_properties) {
  if (!priority.is_primary_for_dns) {
    return;
  }
  std::vector<std::string> dns_servers;
  std::vector<std::string> domain_search;
  std::set<std::string> domain_search_dedup;
  // When DNS information is available from both IPv6 source (RDNSS) and IPv4
  // source (DHCPv4), the ideal behavior is happy eyeballs (RFC 8305). When
  // happy eyeballs is not implemented, the priority of DNS servers are not
  // strictly defined by standard. Prefer IPv6 here as most of the RFCs just
  // "assume" IPv6 is preferred.
  for (const auto* properties : {ipv6_properties, ipv4_properties}) {
    if (!properties) {
      continue;
    }
    dns_servers.insert(dns_servers.end(), properties->dns_servers.begin(),
                       properties->dns_servers.end());

    for (const auto& item : properties->domain_search) {
      if (domain_search_dedup.count(item) == 0) {
        domain_search.push_back(item);
        domain_search_dedup.insert(item);
      }
    }
    if (properties->domain_search.empty() && !properties->domain_name.empty()) {
      auto search_list_derived = properties->domain_name + ".";
      if (domain_search_dedup.count(search_list_derived) == 0) {
        domain_search.push_back(search_list_derived);
        domain_search_dedup.insert(search_list_derived);
      }
    }
  }
  resolver_->SetDNSFromLists(dns_servers, domain_search);
}

void NetworkApplier::ApplyRoutingPolicy(
    int interface_index,
    const std::string& interface_name,
    Technology technology,
    NetworkPriority priority,
    const std::vector<net_base::IPCIDR>& all_addresses,
    const std::vector<net_base::IPv4CIDR>& rfc3442_dsts) {
  uint32_t rule_priority =
      kDefaultPriority + priority.ranking_order * kPriorityStep;
  uint32_t table_id = RoutingTable::GetInterfaceTableId(interface_index);
  bool is_primary_physical = priority.is_primary_physical;
  rule_table_->FlushRules(interface_index);

  // b/180521518: IPv6 routing rules are always omitted for a Cellular
  // connection that is not the primary physical connection. This prevents
  // applications from accidentally using the Cellular network and causing data
  // charges with IPv6 traffic when the primary physical connection is IPv4
  // only.
  bool no_ipv6 = technology == Technology::kCellular && !is_primary_physical;

  // b/189952150: when |no_ipv6| is true and shill must prevent IPv6 traffic on
  // this connection for applications, it is still necessary to ensure that some
  // critical system IPv6 traffic can be routed. Example: shill portal detection
  // probes when the network connection is IPv6 only. For the time being the
  // only supported case is traffic from shill.
  uint32_t shill_uid = rule_table_->GetShillUid();

  // b/177620923 Add uid rules just before the default rule to route to the VPN
  // interface any untagged traffic owner by a uid routed through VPN
  // connections. These rules are necessary for consistency between source IP
  // address selection algorithm that ignores iptables fwmark tagging rules, and
  // the actual routing of packets that have been tagged in iptables PREROUTING.
  if (technology == Technology::kVPN) {
    for (const auto& uid : rule_table_->GetUserTrafficUids()) {
      for (const auto family : net_base::kIPFamilies) {
        auto entry = RoutingPolicyEntry(family);
        entry.priority = kVpnUidRulePriority;
        entry.table = table_id;
        entry.uid_range = fib_rule_uid_range{uid, uid};
        rule_table_->AddRule(interface_index, entry);
      }
    }
  }

  if (is_primary_physical) {
    // Main routing table contains kernel-added routes for source address
    // selection. Sending traffic there before all other rules for physical
    // interfaces (but after any VPN rules) ensures that physical interface
    // rules are not inadvertently too aggressive. Since this rule is static,
    // add it as interface index -1 so it never get removed by FlushRules().
    // Note that this rule could be added multiple times when default network
    // changes, but since the rule itself is identical, there will only be one
    // instance added into kernel.
    for (const auto family : net_base::kIPFamilies) {
      auto main_table_rule = RoutingPolicyEntry(family);
      main_table_rule.priority = kPhysicalPriorityOffset;
      main_table_rule.table = RT_TABLE_MAIN;
      rule_table_->AddRule(-1, main_table_rule);
    }
    // Add a default routing rule to use the primary interface if there is
    // nothing better.
    // TODO(crbug.com/999589) Remove this rule.
    for (const auto family : net_base::kIPFamilies) {
      auto catch_all_rule = RoutingPolicyEntry(family);
      catch_all_rule.priority = kCatchallPriority;
      catch_all_rule.table = table_id;
      rule_table_->AddRule(interface_index, catch_all_rule);
    }
  }

  if (technology != Technology::kVPN) {
    rule_priority += kPhysicalPriorityOffset;
  }

  // Allow for traffic corresponding to this Connection to match with
  // |table_id|. Note that this does *not* necessarily imply that the traffic
  // will actually be routed through a route in |table_id|. For example, if the
  // traffic matches one of the excluded destination addresses set up in
  // SetupExcludedRoutes, then no routes in the per-Device table for this
  // Connection will be used for that traffic.
  for (const auto& dst_address : rfc3442_dsts) {
    auto dst_addr_rule = RoutingPolicyEntry(net_base::IPFamily::kIPv4);
    dst_addr_rule.dst = net_base::IPCIDR(dst_address);
    dst_addr_rule.priority = kDstRulePriority;
    dst_addr_rule.table = table_id;
    rule_table_->AddRule(interface_index, dst_addr_rule);
  }

  // Always set a rule for matching traffic tagged with the fwmark routing tag
  // corresponding to this network interface.
  for (const auto family : net_base::kIPFamilies) {
    auto fwmark_routing_entry = RoutingPolicyEntry(family);
    fwmark_routing_entry.priority = rule_priority;
    fwmark_routing_entry.table = table_id;
    fwmark_routing_entry.fw_mark = GetFwmarkRoutingTag(interface_index);
    if (no_ipv6 && fwmark_routing_entry.family == net_base::IPFamily::kIPv6) {
      fwmark_routing_entry.uid_range = fib_rule_uid_range{shill_uid, shill_uid};
    }
    rule_table_->AddRule(interface_index, fwmark_routing_entry);
  }

  // Add output interface rule for all interfaces, such that SO_BINDTODEVICE can
  // be used without explicitly binding the socket.
  for (const auto family : net_base::kIPFamilies) {
    auto oif_rule = RoutingPolicyEntry(family);
    oif_rule.priority = rule_priority;
    oif_rule.table = table_id;
    oif_rule.oif_name = interface_name;
    if (no_ipv6 && oif_rule.family == net_base::IPFamily::kIPv6) {
      oif_rule.uid_range = fib_rule_uid_range{shill_uid, shill_uid};
    }
    rule_table_->AddRule(interface_index, oif_rule);
  }

  if (technology != Technology::kVPN) {
    // Select the per-device table if the outgoing packet's src address matches
    // the interface's addresses or the input interface is this interface.
    for (const auto& address : all_addresses) {
      auto if_addr_rule = RoutingPolicyEntry(address.GetFamily());
      if_addr_rule.src = address;
      if_addr_rule.table = table_id;
      if_addr_rule.priority = rule_priority;
      if (address.GetFamily() == net_base::IPFamily::kIPv6 && no_ipv6) {
        if_addr_rule.uid_range = fib_rule_uid_range{shill_uid, shill_uid};
      }
      rule_table_->AddRule(interface_index, if_addr_rule);
    }

    for (const auto family : net_base::kIPFamilies) {
      auto iif_rule = RoutingPolicyEntry(family);
      iif_rule.priority = rule_priority;
      iif_rule.table = table_id;
      iif_rule.iif_name = interface_name;
      if (no_ipv6 && iif_rule.family == net_base::IPFamily::kIPv6) {
        iif_rule.uid_range = fib_rule_uid_range{shill_uid, shill_uid};
      }
      rule_table_->AddRule(interface_index, iif_rule);
    }
  }
  proc_fs_->FlushRoutingCache();
}

void NetworkApplier::ApplyMTU(int interface_index, int mtu) {
  rtnl_handler_->SetInterfaceMTU(interface_index, mtu);
}

void NetworkApplier::ApplyRoute(
    int interface_index,
    net_base::IPFamily family,
    const std::optional<net_base::IPAddress>& gateway,
    bool fix_gateway_reachability,
    bool default_route,
    bool blackhole_ipv6,
    const std::vector<net_base::IPCIDR>& excluded_routes,
    const std::vector<net_base::IPCIDR>& included_routes,
    const std::vector<std::pair<net_base::IPv4CIDR, net_base::IPv4Address>>&
        rfc3442_routes) {
  const uint32_t table_id = RoutingTable::GetInterfaceTableId(interface_index);
  CHECK(!gateway || gateway->GetFamily() == family);
  IPAddress empty_ip =
      IPAddress::CreateFromFamily(net_base::ToSAFamily(family));

  // 0. Flush existing routes set by shill.
  routing_table_->FlushRoutesWithTag(interface_index, family);

  // 1. Fix gateway reachablity (add an on-link /32 route to the gateway) if
  // the gateway is not currently on-link. Note this only applies for IPv4 as
  // IPv6 uses the link local address for gateway.
  if (fix_gateway_reachability) {
    CHECK(gateway);
    CHECK(gateway->GetFamily() == net_base::IPFamily::kIPv4);
    net_base::IPv4CIDR gateway_only =
        *net_base::IPv4CIDR::CreateFromAddressAndPrefix(
            *gateway->ToIPv4Address(), 32);
    auto entry = RoutingTableEntry::Create(
                     IPAddress(gateway_only),
                     IPAddress::CreateFromFamily(IPAddress::kFamilyIPv4),
                     IPAddress::CreateFromFamily(IPAddress::kFamilyIPv4))
                     .SetScope(RT_SCOPE_LINK)
                     .SetTable(table_id)
                     .SetType(RTN_UNICAST)
                     .SetTag(interface_index);
    if (!routing_table_->AddRoute(interface_index, entry)) {
      LOG(ERROR) << "Unable to add link-scoped route to gateway " << entry
                 << ", if " << interface_index;
    }
  }

  // 2. Default route and IPv6 blackhole route
  if (default_route) {
    if (!routing_table_->SetDefaultRoute(
            interface_index, gateway ? IPAddress(*gateway) : empty_ip,
            table_id)) {
      LOG(ERROR) << "Unable to add default route via "
                 << (gateway ? gateway->ToString() : "onlink") << ", if "
                 << interface_index;
    }
  }

  if (blackhole_ipv6) {
    if (!routing_table_->CreateBlackholeRoute(
            interface_index, IPAddress::kFamilyIPv6, 0, table_id)) {
      LOG(ERROR) << "Unable to add IPv6 blackhole route, if "
                 << interface_index;
    }
  }

  // 3. Excluded Routes
  // Since each Network has its own dedicated routing table, exclusion is as
  // simple as adding an RTN_THROW entry for each item on the list. Traffic that
  // matches the RTN_THROW entry will cause the kernel to stop traversing our
  // routing table and try the next rule in the list.
  for (const auto& excluded_prefix : excluded_routes) {
    if (excluded_prefix.GetFamily() != family) {
      LOG(WARNING) << "Unmatched IP family for excluded route "
                   << excluded_prefix;
      continue;
    }
    auto entry = RoutingTableEntry::Create(empty_ip, empty_ip, empty_ip)
                     .SetScope(RT_SCOPE_LINK)
                     .SetTable(table_id)
                     .SetType(RTN_THROW)
                     .SetTag(interface_index);
    entry.dst = IPAddress(excluded_prefix);
    if (!routing_table_->AddRoute(interface_index, entry)) {
      LOG(WARNING) << "Unable to setup excluded route " << entry << ", if "
                   << interface_index;
    }
  }

  // 4. Included Routes
  for (const auto& included_prefix : included_routes) {
    if (included_prefix.GetFamily() != family) {
      LOG(WARNING) << "Unmatched IP family for included route "
                   << included_prefix;
      continue;
    }
    auto entry =
        RoutingTableEntry::Create(IPAddress(included_prefix), empty_ip,
                                  gateway ? IPAddress(*gateway) : empty_ip)
            .SetTable(table_id)
            .SetTag(interface_index);
    if (!routing_table_->AddRoute(interface_index, entry)) {
      LOG(WARNING) << "Unable to setup included route " << entry << ", if "
                   << interface_index;
    }
  }

  // 5. RFC 3442 Static Classless Routes from DHCPv4
  for (const auto& [route_prefix, route_gateway] : rfc3442_routes) {
    IPAddress empty_ip = IPAddress::CreateFromFamily(IPAddress::kFamilyIPv4);
    auto entry = RoutingTableEntry::Create(IPAddress(route_prefix), empty_ip,
                                           IPAddress(route_gateway))
                     .SetTable(table_id)
                     .SetTag(interface_index);
    if (!routing_table_->AddRoute(interface_index, entry)) {
      LOG(WARNING) << "Unable to setup static classless route " << entry
                   << ", if " << interface_index;
    }
  }
}

void NetworkApplier::ApplyAddress(
    int interface_index,
    const net_base::IPCIDR& local,
    const std::optional<net_base::IPv4Address>& broadcast) {
  if (address_service_->RemoveAddressOtherThan(interface_index, local)) {
    // The address has changed for this interface.
    LOG(INFO) << __func__ << ": Flushing old addresses.";
  }
  address_service_->AddAddress(interface_index, local, broadcast);
}

}  // namespace shill
