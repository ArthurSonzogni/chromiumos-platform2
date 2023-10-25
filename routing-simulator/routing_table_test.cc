// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "routing-simulator/routing_table.h"

#include <string_view>
#include <utility>

#include <gtest/gtest.h>

#include "net-base/ip_address.h"
#include "routing-simulator/route.h"

namespace routing_simulator {
namespace {

TEST(RoutingTableTest, DefaultConstructor) {
  RoutingTable default_routing_table;
  EXPECT_EQ(default_routing_table.routes(), std::vector<Route>({}));
  EXPECT_EQ(default_routing_table, RoutingTable());
}

TEST(RoutingTableTest, IPv4Constructor) {
  RoutingTable routing_table;
  std::vector<Route> routes;
  for (const auto route_str :
       {"192.25.0.0/24 dev eth0", "192.25.25.0/24 dev eth1"}) {
    const auto route =
        Route::CreateFromRouteString(route_str, net_base::IPFamily::kIPv4)
            .value();
    routing_table.AddRoute(route);
    routes.push_back(route);
  }
  EXPECT_EQ(routing_table.routes(), routes);
}

TEST(RoutingTableTest, IPv6Constructor) {
  RoutingTable routing_table;
  std::vector<Route> routes;
  for (const auto route_str :
       {"2001:0DB8:0:CD30:123:4567:89AB:CDEF/60 dev eth0",
        "2001:0DB8:0:CD30:123:4567:89AB:CDEF/60 dev eth1"}) {
    const auto route =
        Route::CreateFromRouteString(route_str, net_base::IPFamily::kIPv6)
            .value();
    routing_table.AddRoute(route);
    routes.push_back(route);
  }
  EXPECT_EQ(routing_table.routes(), routes);
}

TEST(RoutingTableTest, AddRouteSuccess) {
  std::vector<Route> routes;
  RoutingTable routing_table;
  for (const auto route_str :
       {"192.25.0.0/24 dev eth0", "192.25.25.0/24 dev eth1"}) {
    const auto route =
        Route::CreateFromRouteString(route_str, net_base::IPFamily::kIPv4)
            .value();
    routes.push_back(route);
    routing_table.AddRoute(route);
    EXPECT_EQ(routing_table.routes(), routes);
  }
}

TEST(RoutingTableTest, IPv4LookUpRoute) {
  RoutingTable routing_table;
  for (const auto route_str :
       {"192.25.0.0/24 dev eth0", "192.25.25.0/24 dev eth1",
        "192.25.0.0/16 dev eth3"}) {
    const auto route =
        Route::CreateFromRouteString(route_str, net_base::IPFamily::kIPv4)
            .value();
    routing_table.AddRoute(route);
  }
  const auto dst_ip_case =
      std::vector<std::pair<std::string_view, std::string_view>>(
          {{"192.25.0.1", "eth0"}, {"192.25.25.1", "eth1"}});
  for (const auto& dst_ip_case : dst_ip_case) {
    auto destination_address =
        net_base::IPAddress::CreateFromString(dst_ip_case.first).value();
    auto* matched_route_ptr = routing_table.LookUpRoute(destination_address);
    ASSERT_NE(matched_route_ptr, nullptr);
    EXPECT_EQ(matched_route_ptr->output_interface(), dst_ip_case.second);
  }

  // Test the case when no match route is found.
  auto destination_address =
      net_base::IPAddress::CreateFromString("168.25.25.1").value();
  auto* matched_route_ptr = routing_table.LookUpRoute(destination_address);
  EXPECT_EQ(matched_route_ptr, nullptr);

  // Test the special case (e.g. "default" or no prefix in route strings)
  for (const auto route_str :
       {"default dev eth_default", "192.25.0.1 dev eth_no_prefix"}) {
    const auto route =
        Route::CreateFromRouteString(route_str, net_base::IPFamily::kIPv4)
            .value();
    routing_table.AddRoute(route);
  }
  const auto new_dst_ip_case =
      std::vector<std::pair<std::string_view, std::string_view>>({
          {"192.25.0.1", "eth_no_prefix"},
          {"192.25.25.1", "eth1"},
          {"168.25.25.1", "eth_default"},
      });
  for (const auto& dst_ip_case : new_dst_ip_case) {
    auto destination_address =
        net_base::IPAddress::CreateFromString(dst_ip_case.first).value();
    auto* matched_route_ptr = routing_table.LookUpRoute(destination_address);
    ASSERT_NE(matched_route_ptr, nullptr);
    EXPECT_EQ(matched_route_ptr->output_interface(), dst_ip_case.second);
  }
}

TEST(RoutingTableTest, IPv6LookUpRoute) {
  RoutingTable routing_table;
  for (const auto route_str : {"2401:fa00:480:ee08::/64 dev eth0",
                               "2401:fa00:480:ee08:10::/80 dev eth1",
                               "2401:fa00:480:ee08::/48 dev eth2"}) {
    const auto route =
        Route::CreateFromRouteString(route_str, net_base::IPFamily::kIPv6)
            .value();
    routing_table.AddRoute(route);
  }
  const auto dst_ip_case =
      std::vector<std::pair<std::string_view, std::string_view>>({
          {"2401:fa00:480:ee08::", "eth0"},
          {"2401:fa00:480:ee08:10::", "eth1"},
      });
  for (const auto& dst_ip_case : dst_ip_case) {
    auto destination_address =
        net_base::IPAddress::CreateFromString(dst_ip_case.first).value();
    auto* matched_route_ptr = routing_table.LookUpRoute(destination_address);
    ASSERT_NE(matched_route_ptr, nullptr);
    EXPECT_EQ(matched_route_ptr->output_interface(), dst_ip_case.second);
  }

  // Test the case when no match route is found.
  auto destination_address =
      net_base::IPAddress::CreateFromString("1900:fa00:480:ee08::").value();
  auto* matched_route_ptr = routing_table.LookUpRoute(destination_address);
  EXPECT_EQ(matched_route_ptr, nullptr);

  // Test the special case (e.g. "default" or no prefix length in route strings)
  for (const auto route_str : {"default dev eth_default",
                               "2401:fa00:480:ee08:10:: dev eth_no_prefix"}) {
    const auto route =
        Route::CreateFromRouteString(route_str, net_base::IPFamily::kIPv6)
            .value();
    routing_table.AddRoute(route);
  }
  const auto new_dst_ip_case =
      std::vector<std::pair<std::string_view, std::string_view>>({
          {"2401:fa00:480:ee08:10::", "eth_no_prefix"},
          {"2401:fa00:480:ee08:1::", "eth0"},
          {"1900:fa00:480:ee08::", "eth_default"},
      });
  for (const auto& dst_ip_case : new_dst_ip_case) {
    auto destination_address =
        net_base::IPAddress::CreateFromString(dst_ip_case.first).value();
    auto* matched_route_ptr = routing_table.LookUpRoute(destination_address);
    ASSERT_NE(matched_route_ptr, nullptr);
    EXPECT_EQ(matched_route_ptr->output_interface(), dst_ip_case.second);
  }
}

}  // namespace
}  // namespace routing_simulator
