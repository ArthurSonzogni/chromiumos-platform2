// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "routing-simulator/route_manager.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <base/containers/span.h>
#include <base/strings/string_split.h>
#include <net-base/ip_address.h>

#include "routing-simulator/packet.h"
#include "routing-simulator/routing_policy_entry.h"
#include "routing-simulator/routing_table.h"

namespace routing_simulator {
namespace {

// TODO(b/307460180): Add implementations.
// Executes 'ip rule' according to the ip family.
std::string ExecuteIPRule(net_base::IPFamily ip_family) {
  std::string output;
  return output;
}

// TODO(b/307460180): Add implementations.
// Executes 'ip route show table all' according to the ip family.
std::string ExecuteIPRoute(net_base::IPFamily ip_family) {
  std::string output;
  return output;
}

// TODO(b/307460180): Add implementations.
// Builds a routing policy table from |output|.
void BuildRoutingPolicyTable(
    net_base::IPFamily ip_family,
    std::string_view output,
    std::vector<RoutingPolicyEntry>* routing_policy_table_ptr) {}

// TODO(b/307460180): Add implementations.
// Builds routing tables from |output|.
void BuildRoutingTable(
    net_base::IPFamily ip_family,
    std::string_view output,
    std::map<std::string_view, RoutingTable>* routing_table_map_ptr) {}

// TODO(b/307460180): Add implementations.
// Looks up a route which matches a packet input referring to the routing policy
// table and routing tables and returns the matched route. Returns std::nullopt
// if no matched route is found.
const Route* LookUpRoute(const Packet& packet) {
  return nullptr;
}

}  // namespace

RouteManager::RouteManager() = default;

void RouteManager::BuildTables() {
  const auto policy_output_ipv4 = ExecuteIPRule(net_base::IPFamily::kIPv4);
  const auto policy_output_ipv6 = ExecuteIPRule(net_base::IPFamily::kIPv6);
  const auto route_output_ipv4 = ExecuteIPRoute(net_base::IPFamily::kIPv4);
  const auto route_output_ipv6 = ExecuteIPRoute(net_base::IPFamily::kIPv6);
  BuildRoutingPolicyTable(net_base::IPFamily::kIPv4, policy_output_ipv4,
                          &routing_policy_table_ipv4_);
  BuildRoutingPolicyTable(net_base::IPFamily::kIPv6, policy_output_ipv6,
                          &routing_policy_table_ipv6_);
  BuildRoutingTable(net_base::IPFamily::kIPv4, route_output_ipv4,
                    &routing_tables_ipv4_);
  BuildRoutingTable(net_base::IPFamily::kIPv6, route_output_ipv6,
                    &routing_tables_ipv6_);
}

// TODO(b/307460180): Change the return value to contains both matched
// route and the related policy entry.
const Route* RouteManager::ProcessPacketWithMutation(Packet& packet) {
  const auto matched_route_ptr = LookUpRoute(packet);
  if (matched_route_ptr) {
    packet.output_interface() = matched_route_ptr->output_interface();
  }
  return matched_route_ptr;
}

}  // namespace routing_simulator
