// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "routing-simulator/routing_table.h"

#include <string_view>

#include <gtest/gtest.h>

#include "net-base/ip_address.h"
#include "routing-simulator/route.h"

namespace routing_simulator {
namespace {

TEST(RoutingSimulatorTest, DefaultConstructor) {
  RoutingTable default_routing_table;
  EXPECT_EQ(default_routing_table.routes(), std::vector<Route>({}));
  EXPECT_EQ(default_routing_table, RoutingTable());
}

TEST(RoutingSimulatorTest, IPv4Constructor) {
  std::vector<Route> routes;
  for (const auto route_str :
       {"192.25.0.0/24 dev eth0", "192.25.25.0/24 dev eth1"}) {
    const auto new_route = Route::CreateFromRouteString(route_str).value();
    routes.push_back(new_route);
  }
  RoutingTable routing_table(routes);
  EXPECT_EQ(routing_table.routes(), routes);
}

TEST(RoutingSimulatorTest, IPv6Constructor) {
  std::vector<Route> routes;
  for (const auto route_str :
       {"2001:0DB8:0:CD30:123:4567:89AB:CDEF/60 dev eth0",
        "2001:0DB8:0:CD30:123:4567:89AB:CDEF/60 dev eth1"}) {
    const auto new_route = Route::CreateFromRouteString(route_str).value();
    routes.push_back(new_route);
  }
  RoutingTable routing_table(routes);
  EXPECT_EQ(routing_table.routes(), routes);
}

TEST(RoutingSimulatorTest, AddRouteSuccess) {
  std::vector<Route> routes;
  RoutingTable routing_table;
  for (const auto route_str :
       {"192.25.0.0/24 dev eth0", "192.25.25.0/24 dev eth1"}) {
    const auto new_route = Route::CreateFromRouteString(route_str).value();
    routes.push_back(new_route);
    routing_table.AddRoute(new_route);
    EXPECT_EQ(routing_table.routes(), routes);
  }
}

TEST(RoutingSimulatorTest, LookUpRouteSuccess) {
  RoutingTable routing_table;
  for (const auto route_str :
       {"192.25.0.0/24 dev eth0", "192.25.25.0/24 dev eth1",
        "192.25.0.0/16 dev eth3"}) {
    const auto new_route = Route::CreateFromRouteString(route_str).value();
    routing_table.AddRoute(new_route);
  }
  EXPECT_EQ(routing_table.LookUpRoute(
                net_base::IPAddress::CreateFromString("192.25.0.1").value()),
            "eth0");
  EXPECT_EQ(routing_table.LookUpRoute(
                net_base::IPAddress::CreateFromString("192.25.25.1").value()),
            "eth1");
  EXPECT_EQ(routing_table.LookUpRoute(
                net_base::IPAddress::CreateFromString("168.25.25.1").value()),
            std::nullopt);
}

// TODO(snamika): implement Test func below
// - Verify that RoutingTable class can handle routes with /0 and /32 properly.
}  // namespace
}  // namespace routing_simulator
