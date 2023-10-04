// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "routing-simulator/route.h"

#include <gtest/gtest.h>

namespace routing_simulator {
namespace {

TEST(RouteTest, CreateFromRouteStringIPv4Success) {
  const auto prefix_eth1 =
      net_base::IPCIDR::CreateFromCIDRString("192.25.25.0/24").value();
  std::string output_interface_eth1 = "eth1";
  const auto route_eth1 =
      Route::CreateFromRouteString("192.25.25.0/24 dev eth1");
  ASSERT_TRUE(route_eth1.has_value());
  EXPECT_EQ(route_eth1.value().prefix(), prefix_eth1);
  EXPECT_EQ(route_eth1.value().output_interface(), output_interface_eth1);
}

TEST(RouteTest, CreateFromRouteStringIPv6Success) {
  const auto prefix_eth1 = net_base::IPCIDR::CreateFromCIDRString(
                               "2100:0DB8:0:CD30:123:4567:89AB:CDEF/60")
                               .value();
  std::string output_interface_eth1 = "eth1";
  const auto route_eth1 = Route::CreateFromRouteString(
      "2100:0DB8:0:CD30:123:4567:89AB:CDEF/60 dev eth1");
  ASSERT_TRUE(route_eth1.has_value());
  EXPECT_EQ(route_eth1.value().prefix(), prefix_eth1);
  EXPECT_EQ(route_eth1.value().output_interface(), output_interface_eth1);
}

TEST(RouteTest, CreateFromRouteStringFail) {
  EXPECT_EQ(Route::CreateFromRouteString("192.25.25.0/24 eth1"), std::nullopt);
  EXPECT_EQ(Route::CreateFromRouteString("192.25.25.0/24 dev"), std::nullopt);
  EXPECT_EQ(Route::CreateFromRouteString("dev eth0"), std::nullopt);
}

TEST(RouteTest, CreateFromRouteStringHandleSpaceSuccess) {
  const auto prefix =
      net_base::IPCIDR::CreateFromCIDRString("192.25.25.0/24").value();
  std::string output_interface = "eth1";
  for (const auto route_str :
       {"192.25.25.0/24    dev eth1", "192.25.25.0/24 dev eth1  "}) {
    const auto route = Route::CreateFromRouteString(route_str);
    ASSERT_TRUE(route.has_value());
    EXPECT_EQ(route.value().prefix(), prefix);
    EXPECT_EQ(route.value().output_interface(), output_interface);
  }
}
}  // namespace
}  // namespace routing_simulator
