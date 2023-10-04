// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "routing-simulator/routing_table.h"

#include <string>
#include <vector>

#include <base/logging.h>
#include <net-base/ip_address.h>

#include "routing-simulator/route.h"

namespace routing_simulator {
RoutingTable::RoutingTable() = default;
RoutingTable::RoutingTable(const std::vector<Route>& routes)
    : routes_(routes) {}

RoutingTable::RoutingTable(const RoutingTable& other) = default;
RoutingTable& RoutingTable::operator=(const RoutingTable& other) = default;

void RoutingTable::AddRoute(const Route& new_route) {
  routes_.push_back(new_route);
}

// Returns the output interface of the route which matches the destination.
// Returns std::nullopt if the format is invalid or no match route is found.
std::optional<std::string> RoutingTable::LookUpRoute(
    const net_base::IPAddress& address) {
  const Route* longest_prefix_route_ptr = nullptr;
  for (const auto& route : routes_) {
    if (!route.prefix().InSameSubnetWith(address)) {
      continue;
    }
    if (longest_prefix_route_ptr == nullptr) {
      longest_prefix_route_ptr = &route;
      continue;
    }
    if (longest_prefix_route_ptr->prefix().prefix_length() <
        route.prefix().prefix_length()) {
      longest_prefix_route_ptr = &route;
    }
  }
  // No match route is found.
  if (longest_prefix_route_ptr == nullptr) {
    return std::nullopt;
  }
  return longest_prefix_route_ptr->output_interface();
}

bool RoutingTable::operator==(const RoutingTable& rhs) const {
  return routes_ == rhs.routes_;
}
}  // namespace routing_simulator
