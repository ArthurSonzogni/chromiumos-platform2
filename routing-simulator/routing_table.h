// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ROUTING_SIMULATOR_ROUTING_TABLE_H_
#define ROUTING_SIMULATOR_ROUTING_TABLE_H_

#include <string>
#include <vector>

#include "routing-simulator/route.h"

namespace routing_simulator {

// Represents a routing table that consists of a list of routes and supports
// queries based on longest prefix matching.
class RoutingTable {
 public:
  RoutingTable();
  explicit RoutingTable(const std::vector<Route>& routes);

  // RoutingTable is only copyable.
  RoutingTable(const RoutingTable& other);
  RoutingTable& operator=(const RoutingTable& other);

  // Adds a new route to a routing table.
  void AddRoute(const Route& new_route);

  // Returns the output interface of the route which is in the same subnet as
  // the the destination IP address and the prefix length of which is the
  // longest among the routing table.
  // Returns std::nullopt if the format is invalid or no match route is found.
  std::optional<std::string> LookUpRoute(const net_base::IPAddress& address);

  // Getter methods for the internal data.
  const std::vector<Route>& routes() const { return routes_; }

  bool operator==(const RoutingTable& rhs) const;

 private:
  std::vector<Route> routes_;
};

}  // namespace routing_simulator

#endif  // ROUTING_SIMULATOR_ROUTING_TABLE_H_
