// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/network/network_applier.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <chromeos/net-base/ip_address.h>
#include <chromeos/net-base/ipv4_address.h>
#include <chromeos/net-base/network_priority.h>
#include <chromeos/net-base/proc_fs_stub.h>

#include "patchpanel/network/address_service.h"
#include "patchpanel/network/routing_policy_service.h"
#include "patchpanel/network/routing_table.h"
#include "patchpanel/network/routing_table_entry.h"
#include "patchpanel/routing_service.h"

namespace patchpanel {

namespace {
// TODO(b/161507671) Use the constants defined in patchpanel::RoutingService at
// platform2/patchpanel/routing_service.cc.
constexpr const uint32_t kFwmarkRoutingMask = 0xffff0000;

// kCrosVmFwmark = {.value = 0x2100, .mask = 0x3f00} should be the preferred
// method to match traffic from crosvm. This is a workaround before b/300033608
// is fixed.
// From patchpanel/address_manager.cc:
//   100.115.92.24 - 100.115.92.127 for CrosVM;
//   100.115.92.192 - 100.115.92.255 for Crostini containers.
const auto kCrosVmSrcIP = {*net_base::IPv4CIDR::CreateFromAddressAndPrefix(
                               net_base::IPv4Address(100, 115, 92, 24), 29),
                           *net_base::IPv4CIDR::CreateFromAddressAndPrefix(
                               net_base::IPv4Address(100, 115, 92, 32), 27),
                           *net_base::IPv4CIDR::CreateFromAddressAndPrefix(
                               net_base::IPv4Address(100, 115, 92, 64), 26),
                           *net_base::IPv4CIDR::CreateFromAddressAndPrefix(
                               net_base::IPv4Address(100, 115, 92, 192), 26)};

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
    RoutingPolicyService::kRulePriorityMain - 5;
// Priority for rules routing traffic from certain VMs through CLAT.
constexpr uint32_t kClatRulePriority =
    RoutingPolicyService::kRulePriorityMain - 4;
// Priority for rules routing traffic with BYPASS_VPN mark to the default
// physical network.
constexpr uint32_t kBypassVpnRulePriority =
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
    : rule_table_(std::make_unique<RoutingPolicyService>()),
      routing_table_(std::make_unique<RoutingTable>()),
      address_service_(std::make_unique<AddressService>(routing_table_.get())),
      rtnl_handler_(net_base::RTNLHandler::GetInstance()),
      proc_fs_(std::make_unique<net_base::ProcFsStub>("")) {}

NetworkApplier::~NetworkApplier() = default;

// static
NetworkApplier* NetworkApplier::GetInstance() {
  static base::NoDestructor<NetworkApplier> instance;
  return instance.get();
}

// static
std::unique_ptr<NetworkApplier> NetworkApplier::CreateForTesting(
    std::unique_ptr<RoutingTable> routing_table,
    std::unique_ptr<RoutingPolicyService> rule_table,
    std::unique_ptr<AddressService> address_service,
    net_base::RTNLHandler* rtnl_handler,
    std::unique_ptr<net_base::ProcFsStub> proc_fs) {
  // Using `new` to access a non-public constructor.
  auto ptr = base::WrapUnique(new NetworkApplier());
  ptr->routing_table_ = std::move(routing_table);
  ptr->rule_table_ = std::move(rule_table);
  ptr->address_service_ = std::move(address_service);
  ptr->rtnl_handler_ = rtnl_handler;
  ptr->proc_fs_ = std::move(proc_fs);
  return ptr;
}

void NetworkApplier::Start() {
  routing_table_->Start();
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
  rtnl_handler_->SetInterfaceMTU(interface_index,
                                 net_base::NetworkConfig::kDefaultMTU);
}

void NetworkApplier::ApplyDNS(
    net_base::NetworkPriority priority,
    const std::vector<net_base::IPAddress>& dns_servers,
    const std::vector<std::string>& dns_search_domains) {
  // TODO(b/259354228): Notify dnsproxy when DNS changes. Note that currently
  // dnsproxy is getting the information from itself subscribing to patchpanel
  // Device/Service event API instead.
}

void NetworkApplier::ApplyRoutingPolicy(
    int interface_index,
    const std::string& interface_name,
    Technology technology,
    net_base::NetworkPriority priority,
    const std::vector<net_base::IPCIDR>& all_addresses,
    const std::vector<net_base::IPv4CIDR>& rfc3442_dsts) {
  uint32_t rule_priority =
      kDefaultPriority + priority.ranking_order * kPriorityStep;
  uint32_t table_id = RoutingTable::GetInterfaceTableId(interface_index);
  bool is_primary_physical = priority.is_primary_physical;
  rule_table_->FlushRules(interface_index);

  // Add rules just before the default rule to route to the VPN interface for
  // certain traffic. These rules are necessary for consistency between source
  // IP address selection algorithm that ignores iptables fwmark tagging rules,
  // and the actual routing of packets that have been tagged in iptables
  // PREROUTING or OUTPUT.
  if (technology == Technology::kVPN) {
    // b/177620923 Add uid rules any untagged traffic owner by a uid routed
    // through VPN connections.
    for (const auto& uid : rule_table_->GetUserTrafficUids()) {
      for (const auto family : net_base::kIPFamilies) {
        auto entry = RoutingPolicyEntry(family);
        entry.priority = kVpnUidRulePriority;
        entry.table = table_id;
        entry.uid_range = uid.second;
        rule_table_->AddRule(interface_index, entry);
      }
    }

    // Add rules for packets already tagged with ROUTE_ON_VPN.
    for (const auto family : net_base::kIPFamilies) {
      auto entry = RoutingPolicyEntry(family);
      entry.priority = kVpnUidRulePriority;
      entry.table = table_id;
      entry.fw_mark = RoutingPolicyEntry::FwMark{
          .value = kFwmarkRouteOnVpn.fwmark,
          .mask = kFwmarkVpnMask.fwmark,
      };
      rule_table_->AddRule(interface_index, entry);
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
    // Add a rule right before the VPN uid rules to match packets with
    // BYPASS_VPN mark and point them to the default table. Similar to the VPN
    // uid rules, the main purpose of this rule is for src ip selection,
    // otherwise a packet from a user socket with BYPASS_VPN will be matched by
    // the VPN uid rules. This rule has to have a lower priority than the rules
    // for routing tags.
    for (const auto family : net_base::kIPFamilies) {
      auto rule = RoutingPolicyEntry(family);
      rule.priority = kBypassVpnRulePriority;
      rule.table = table_id;
      rule.fw_mark = RoutingPolicyEntry::FwMark{
          .value = kFwmarkBypassVpn.fwmark,
          .mask = kFwmarkVpnMask.fwmark,
      };
      rule_table_->AddRule(interface_index, rule);
    }
  }

  if (priority.is_primary_logical) {
    // Add a routing rule for IPv4 traffic to look up CLAT table first before it
    // get to catch-all rule.
    for (const auto& src : kCrosVmSrcIP) {
      auto clat_table_rule = RoutingPolicyEntry(net_base::IPFamily::kIPv4);
      clat_table_rule.priority = kClatRulePriority;
      clat_table_rule.table = RoutingTable::kClatRoutingTableId;
      clat_table_rule.src = net_base::IPCIDR(src);
      rule_table_->AddRule(-1, clat_table_rule);
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

  // b/180521518: Add an explicit rule to block user IPv6 traffic for a Cellular
  // connection that is not the primary physical connection. This prevents
  // Chrome from accidentally using the Cellular network and causing data
  // charges with IPv6 traffic when the primary physical connection is IPv4
  // only.
  bool chronos_no_ipv6 =
      technology == Technology::kCellular && !is_primary_physical;
  if (chronos_no_ipv6) {
    auto chrome_uid = rule_table_->GetChromeUid();
    for (const auto& address : all_addresses) {
      if (address.GetFamily() != net_base::IPFamily::kIPv6) {
        continue;
      }
      auto blackhole_chronos_ipv6_rule =
          RoutingPolicyEntry(net_base::IPFamily::kIPv6);
      blackhole_chronos_ipv6_rule.priority = rule_priority - 1;
      blackhole_chronos_ipv6_rule.src = address;
      blackhole_chronos_ipv6_rule.table = RoutingTable::kUnreachableTableId;
      blackhole_chronos_ipv6_rule.uid_range = chrome_uid;
      rule_table_->AddRule(interface_index, blackhole_chronos_ipv6_rule);
    }
  }

  //  Always set a rule for matching traffic tagged with the fwmark routing tag
  //  corresponding to this network interface.
  for (const auto family : net_base::kIPFamilies) {
    auto fwmark_routing_entry = RoutingPolicyEntry(family);
    fwmark_routing_entry.priority = rule_priority;
    fwmark_routing_entry.table = table_id;
    fwmark_routing_entry.fw_mark = GetFwmarkRoutingTag(interface_index);
    rule_table_->AddRule(interface_index, fwmark_routing_entry);
  }

  // Add output interface rule for all interfaces, such that SO_BINDTODEVICE can
  // be used without explicitly binding the socket.
  for (const auto family : net_base::kIPFamilies) {
    auto oif_rule = RoutingPolicyEntry(family);
    oif_rule.priority = rule_priority;
    oif_rule.table = table_id;
    oif_rule.oif_name = interface_name;
    rule_table_->AddRule(interface_index, oif_rule);
  }

  if (technology != Technology::kVPN) {
    // Select the per-device table if the outgoing packet's src address matches
    // the interface's addresses, dst address is in the interface's prefix, or
    // the input interface is this interface.
    for (const auto& address : all_addresses) {
      auto if_addr_rule = RoutingPolicyEntry(address.GetFamily());
      if_addr_rule.src = *net_base::IPCIDR::CreateFromAddressAndPrefix(
          address.address(),
          net_base::IPCIDR::GetMaxPrefixLength(address.GetFamily()));
      if_addr_rule.table = table_id;
      if_addr_rule.priority = rule_priority;
      rule_table_->AddRule(interface_index, if_addr_rule);

      if_addr_rule = RoutingPolicyEntry(address.GetFamily());
      if_addr_rule.dst = address;
      if_addr_rule.table = table_id;
      if_addr_rule.priority = rule_priority;
      rule_table_->AddRule(interface_index, if_addr_rule);
    }

    for (const auto family : net_base::kIPFamilies) {
      auto iif_rule = RoutingPolicyEntry(family);
      iif_rule.priority = rule_priority;
      iif_rule.table = table_id;
      iif_rule.iif_name = interface_name;
      rule_table_->AddRule(interface_index, iif_rule);
    }
  }
  proc_fs_->FlushRoutingCache();
}

void NetworkApplier::ApplyMTU(int interface_index, int mtu) {
  rtnl_handler_->SetInterfaceMTU(interface_index, static_cast<uint32_t>(mtu));
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
  if (gateway && gateway->GetFamily() != family) {
    LOG(DFATAL) << "Gateway address [" << *gateway << "] unmatched with family "
                << family;
    return;
  }
  const uint32_t table_id = RoutingTable::GetInterfaceTableId(interface_index);
  auto empty_ip = net_base::IPCIDR(family);

  // 0. Flush existing routes set by patchpanel.
  routing_table_->FlushRoutesWithTag(interface_index, family);

  // 1. Fix gateway reachability (add an on-link /32 route to the gateway) if
  // the gateway is not currently on-link. Note this only applies for IPv4 as
  // IPv6 uses the link local address for gateway.
  if (fix_gateway_reachability) {
    CHECK(gateway);
    CHECK(gateway->GetFamily() == net_base::IPFamily::kIPv4);
    auto entry = RoutingTableEntry(net_base::IPFamily::kIPv4);
    entry.dst = *net_base::IPCIDR::CreateFromAddressAndPrefix(*gateway, 32);
    entry.scope = RT_SCOPE_LINK;
    entry.table = table_id;
    entry.type = RTN_UNICAST;
    entry.tag = interface_index;
    if (!routing_table_->AddRoute(interface_index, entry)) {
      LOG(ERROR) << "Unable to add link-scoped route to gateway " << entry
                 << ", if " << interface_index;
    }
  }

  // 2. Default route
  if (default_route) {
    if (!routing_table_->SetDefaultRoute(
            interface_index, gateway.value_or(empty_ip.address()), table_id)) {
      LOG(ERROR) << "Unable to add default route via "
                 << (gateway ? gateway->ToString() : "onlink") << ", if "
                 << interface_index;
    }
  }

  // 3. Excluded Routes
  // Since each Network has its own dedicated routing table, exclusion is as
  // simple as adding an RTN_THROW entry for each item on the list. Traffic that
  // matches the RTN_THROW entry will cause the kernel to stop traversing our
  // routing table and try the next rule in the list.
  bool has_ipv6_default_excluded_prefix = false;
  for (const auto& excluded_prefix : excluded_routes) {
    if (excluded_prefix.GetFamily() != family) {
      continue;
    }
    if (excluded_prefix.prefix_length() == 0 &&
        family == net_base::IPFamily::kIPv6) {
      has_ipv6_default_excluded_prefix = true;
    }
    auto entry = RoutingTableEntry(family);
    entry.scope = RT_SCOPE_LINK;
    entry.table = table_id;
    entry.type = RTN_THROW;
    entry.tag = interface_index;
    entry.dst = excluded_prefix;
    if (!routing_table_->AddRoute(interface_index, entry)) {
      LOG(WARNING) << "Unable to setup excluded route " << entry << ", if "
                   << interface_index;
    }
  }

  // 4. Included Routes and IPv6 Blackhole Routes
  for (const auto& included_prefix : included_routes) {
    if (included_prefix.GetFamily() != family) {
      continue;
    }
    auto entry = RoutingTableEntry(family);
    entry.dst = included_prefix;
    if (gateway) {
      entry.gateway = *gateway;
    }
    entry.table = table_id;
    entry.tag = interface_index;
    if (family == net_base::IPFamily::kIPv6 && blackhole_ipv6) {
      entry.type = RTN_BLACKHOLE;
    }
    if (!routing_table_->AddRoute(interface_index, entry)) {
      LOG(WARNING) << "Unable to setup included route " << entry << ", if "
                   << interface_index;
    }
  }
  if (family == net_base::IPFamily::kIPv6 && blackhole_ipv6 &&
      !has_ipv6_default_excluded_prefix) {
    if (!routing_table_->CreateBlackholeRoute(
            interface_index, net_base::IPFamily::kIPv6, 0, table_id)) {
      LOG(ERROR) << "Unable to add IPv6 blackhole route, if "
                 << interface_index;
    }
  }

  // 5. RFC 3442 Static Classless Routes from DHCPv4
  for (const auto& [route_prefix, route_gateway] : rfc3442_routes) {
    auto entry = RoutingTableEntry(net_base::IPFamily::kIPv4);
    entry.dst = net_base::IPCIDR(route_prefix);
    entry.gateway = net_base::IPAddress(route_gateway);
    entry.table = table_id;
    entry.tag = interface_index;
    if (!routing_table_->AddRoute(interface_index, entry)) {
      LOG(WARNING) << "Unable to setup static classless route " << entry
                   << ", if " << interface_index;
    }
  }
}

void NetworkApplier::ApplyNetworkConfig(
    int interface_index,
    const std::string& interface_name,
    Area area,
    const net_base::NetworkConfig& network_config,
    net_base::NetworkPriority priority,
    Technology technology) {
  if (area & Area::kClear) {
    Clear(interface_index);
  }
  if (area & Area::kIPv4Address) {
    if (network_config.ipv4_address) {
      address_service_->SetIPv4Address(interface_index,
                                       *network_config.ipv4_address,
                                       network_config.ipv4_broadcast);
    } else {
      address_service_->ClearIPv4Address(interface_index);
    }
  }
  if (area & Area::kIPv4Route) {
    bool default_route = (area & Area::kIPv4DefaultRoute) &&
                         network_config.included_route_prefixes.empty();

    // Check if an IPv4 gateway is on-link, and add a /32 on-link route to the
    // gateway if not. Note that IPv6 uses link local address for gateway so
    // this is not needed.
    bool fix_gateway_reachability =
        network_config.ipv4_gateway && network_config.ipv4_address &&
        !network_config.ipv4_address->InSameSubnetWith(
            *network_config.ipv4_gateway);
    if (fix_gateway_reachability) {
      LOG(WARNING)
          << interface_name << ": Gateway " << *network_config.ipv4_gateway
          << " is unreachable from local address/prefix "
          << *network_config.ipv4_address
          << ", mitigating this by creating a link route to the gateway.";
    }

    std::optional<net_base::IPAddress> gateway = std::nullopt;
    if (network_config.ipv4_gateway) {
      gateway = net_base::IPAddress(*network_config.ipv4_gateway);
    }

    ApplyRoute(interface_index, net_base::IPFamily::kIPv4, gateway,
               fix_gateway_reachability, default_route,
               /*blackhole_ipv6=*/false, network_config.excluded_route_prefixes,
               network_config.included_route_prefixes,
               network_config.rfc3442_routes);
  }
  if (area & Area::kIPv6Address) {
    address_service_->SetIPv6Addresses(interface_index,
                                       network_config.ipv6_addresses);
  }
  if (area & Area::kIPv6Route) {
    bool default_route = (area & Area::kIPv6DefaultRoute) &&
                         network_config.included_route_prefixes.empty() &&
                         !network_config.ipv6_blackhole_route;

    std::optional<net_base::IPAddress> gateway = std::nullopt;
    if (network_config.ipv6_gateway) {
      gateway = net_base::IPAddress(*network_config.ipv6_gateway);
    }

    ApplyRoute(interface_index, net_base::IPFamily::kIPv6, gateway,
               /*fix_gateway_reachability=*/false, default_route,
               network_config.ipv6_blackhole_route,
               network_config.excluded_route_prefixes,
               network_config.included_route_prefixes, {});
  }
  if (area & Area::kRoutingPolicy) {
    std::vector<net_base::IPCIDR> all_addresses;
    if (network_config.ipv4_address) {
      all_addresses.emplace_back(*network_config.ipv4_address);
    }
    for (const auto& item : network_config.ipv6_addresses) {
      all_addresses.emplace_back(item);
    }
    std::vector<net_base::IPv4CIDR> rfc3442_dsts;
    for (const auto& item : network_config.rfc3442_routes) {
      rfc3442_dsts.push_back(item.first);
    }
    ApplyRoutingPolicy(interface_index, interface_name, technology, priority,
                       all_addresses, rfc3442_dsts);
  }
  if (area & Area::kDNS) {
    ApplyDNS(priority, network_config.dns_servers,
             network_config.dns_search_domains);
  }
  if (area & Area::kMTU) {
    ApplyMTU(interface_index,
             network_config.mtu.value_or(net_base::NetworkConfig::kDefaultMTU));
  }
}

}  // namespace patchpanel
