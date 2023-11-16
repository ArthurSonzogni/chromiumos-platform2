// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ROUTING_SIMULATOR_ROUTING_TABLE_H_
#define ROUTING_SIMULATOR_ROUTING_TABLE_H_

#include <vector>

#include "routing-simulator/route.h"

namespace routing_simulator {

// Represents a routing table that consists of a list of routes and supports
// queries based on longest prefix matching.
class RoutingTable {
 public:
  RoutingTable();

  // RoutingTable is copyable.
  RoutingTable(const RoutingTable& other);
  RoutingTable& operator=(const RoutingTable& other);

  // Adds a new route to a routing table.
  void AddRoute(const Route& new_route);

  // Does the longest prefix matching for |address| among the matched routes in
  // the routing table and returns the matched route whose prefix matches
  // |address|. Returns nullptr if there is no matching route for |address|.
  const Route* LookUpRoute(const net_base::IPAddress& address) const;

  // Getter methods for the internal data.
  const std::vector<Route>& routes() const { return routes_; }

  bool operator==(const RoutingTable& rhs) const;

 private:
  std::vector<Route> routes_;
};

}  // namespace routing_simulator

#endif  // ROUTING_SIMULATOR_ROUTING_TABLE_H_
