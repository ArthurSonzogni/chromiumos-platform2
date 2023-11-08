// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ROUTING_SIMULATOR_ROUTE_MANAGER_H_
#define ROUTING_SIMULATOR_ROUTE_MANAGER_H_

#include <map>
#include <string>
#include <vector>

#include <net-base/ip_address.h>

#include "routing-simulator/packet.h"
#include "routing-simulator/routing_policy_entry.h"
#include "routing-simulator/routing_table.h"

namespace routing_simulator {

// Maintains the internal states in routing policy tables and routing tables,
// and supports 1) looks up a route and policy which matches a input packet,
// 2) takes records of the matched routes and 3) according to the matched route,
// modifies the packet (only output interface for now).
class RouteManager {
 public:
  RouteManager();

  // RouteManager is neither copyable nor movable.
  RouteManager(const RouteManager&) = delete;
  RouteManager& operator=(const RouteManager&) = delete;

  // Builds internal states in a routing policy table and routing tables from
  // the output strings of 'ip rule' and 'ip route show table all' execution for
  // both IPv4 and IPv6.
  void BuildTables();

  // TODO(b/307460180): Support source ip selection by setting source ip
  // according to the matched route.
  // Finds a route which matches a packet input and modify the packet according
  // to the matched route. Returns the matched route, which can be nullptr if no
  // matched route is found.
  const Route* ProcessPacketWithMutation(Packet& packet);

 private:
  std::vector<RoutingPolicyEntry> routing_policy_table_ipv4_;
  std::vector<RoutingPolicyEntry> routing_policy_table_ipv6_;
  // Maps from tables ids to RoutingTable objects.
  std::map<std::string, RoutingTable> routing_tables_ipv4_;
  std::map<std::string, RoutingTable> routing_tables_ipv6_;

  // Looks up a route which matches a packet input referring to the routing
  // policy table and routing tables and returns the matched route. Returns
  // std::nullopt if no matched route is found.
  const Route* LookUpRoute(const Packet& packet) const;
};

}  // namespace routing_simulator

#endif  // ROUTING_SIMULATOR_ROUTE_MANAGER_H_
