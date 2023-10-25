// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "routing-simulator/routing_table.h"

#include <vector>

#include <base/logging.h>
#include <net-base/ip_address.h>

#include "routing-simulator/route.h"

namespace routing_simulator {

RoutingTable::RoutingTable() = default;

RoutingTable::RoutingTable(const RoutingTable& other) = default;
RoutingTable& RoutingTable::operator=(const RoutingTable& other) = default;

void RoutingTable::AddRoute(const Route& new_route) {
  routes_.push_back(new_route);
}

const Route* RoutingTable::LookUpRoute(
    net_base::IPAddress& destination_address) {
  const Route* longest_prefix_route_ptr = nullptr;
  for (const auto& route : routes_) {
    if (!route.destination_prefix().InSameSubnetWith(destination_address)) {
      continue;
    }
    if (longest_prefix_route_ptr == nullptr) {
      longest_prefix_route_ptr = &route;
      continue;
    }
    if (longest_prefix_route_ptr->destination_prefix().prefix_length() <
        route.destination_prefix().prefix_length()) {
      longest_prefix_route_ptr = &route;
    }
  }
  return longest_prefix_route_ptr;
}

bool RoutingTable::operator==(const RoutingTable& rhs) const = default;

}  // namespace routing_simulator
