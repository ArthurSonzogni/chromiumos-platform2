// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "routing-simulator/route.h"

#include <gtest/gtest.h>
#include <net-base/ip_address.h>

namespace routing_simulator {
namespace {

constexpr net_base::IPCIDR kIPv4DefaultPrefix(net_base::IPFamily::kIPv4);
constexpr net_base::IPCIDR kIPv6DefaultPrefix(net_base::IPFamily::kIPv6);

TEST(RouteTest, CreateFromRouteStringIPv4Success) {
  // Route with the "local" route type.
  const auto route_1 = Route::CreateFromRouteString(
      "local 100.115.92.133 dev arc_ns1 proto kernel scope host "
      "src 100.115.92.133",
      net_base::IPFamily::kIPv4);
  ASSERT_TRUE(route_1.has_value());
  EXPECT_EQ(route_1->type(), Route::Type::kLocal);
  EXPECT_EQ(route_1.value().destination_prefix(),
            net_base::IPCIDR::CreateFromCIDRString("100.115.92.133").value());
  EXPECT_EQ(route_1->next_hop(), std::nullopt);
  EXPECT_EQ(route_1->output_interface(), "arc_ns1");
  EXPECT_EQ(route_1->table_id(), "main");

  // Route with the next hop and default destination prefix.
  const auto route_2 = Route::CreateFromRouteString(
      "default via 100.87.84.254 dev eth0 table 1002 metric 65536",
      net_base::IPFamily::kIPv4);
  ASSERT_TRUE(route_2.has_value());
  EXPECT_EQ(route_2->type(), Route::Type::kUnicast);
  EXPECT_EQ(route_2->destination_prefix(), kIPv4DefaultPrefix);
  EXPECT_EQ(route_2->next_hop(),
            net_base::IPAddress::CreateFromString("100.87.84.254").value());
  EXPECT_EQ(route_2->output_interface(), "eth0");
  EXPECT_EQ(route_2->table_id(), "1002");

  // Route with the a certain destination prefix.
  const auto route_3 = Route::CreateFromRouteString(
      "100.115.92.132/30 dev arc_ns1 proto kernel scope link src "
      "100.115.92.133",
      net_base::IPFamily::kIPv4);
  ASSERT_TRUE(route_3.has_value());
  EXPECT_EQ(route_3->type(), Route::Type::kUnicast);
  EXPECT_EQ(
      route_3->destination_prefix(),
      net_base::IPCIDR::CreateFromCIDRString("100.115.92.132/30").value());
  EXPECT_EQ(route_3->next_hop(), std::nullopt);
  EXPECT_EQ(route_3->output_interface(), "arc_ns1");
  EXPECT_EQ(route_3->table_id(), "main");

  // Test the special case (e.g. no prefix length in route strings)
  const auto route_with_no_prefix_length = Route::CreateFromRouteString(
      "local 100.115.92.133 dev arc_ns1 table local proto kernel scope host "
      "src 100.115.92.133",
      net_base::IPFamily::kIPv4);
  const auto prefix =
      net_base::IPCIDR::CreateFromCIDRString("100.115.92.133/32").value();
  EXPECT_EQ(route_with_no_prefix_length.value().destination_prefix(), prefix);
}

TEST(RouteTest, CreateFromRouteStringIPv6Success) {
  // Route with the "unreachable" route type.
  const auto route_1 = Route::CreateFromRouteString(
      "unreachable default dev lo table 250 metric 1024 pref medium",
      net_base::IPFamily::kIPv6);
  ASSERT_TRUE(route_1.has_value());
  EXPECT_EQ(route_1->type(), Route::Type::kUnreachable);
  EXPECT_EQ(route_1->destination_prefix(), kIPv6DefaultPrefix);
  EXPECT_EQ(route_1->next_hop(), std::nullopt);
  EXPECT_EQ(route_1->output_interface(), "lo");
  EXPECT_EQ(route_1->table_id(), "250");

  // Route with the next hop.
  const auto route_2 = Route::CreateFromRouteString(
      "default via fe80::2a00:79e1:abc:f604 dev wlan0 table 1003 proto ra "
      "metric 1024 expires 3534sec hoplimit 64 pref medium",
      net_base::IPFamily::kIPv6);
  ASSERT_TRUE(route_2.has_value());
  EXPECT_EQ(route_2->type(), Route::Type::kUnicast);
  EXPECT_EQ(route_2->destination_prefix(), kIPv6DefaultPrefix);
  EXPECT_EQ(route_2->next_hop(),
            net_base::IPAddress::CreateFromString("fe80::2a00:79e1:abc:f604")
                .value());
  EXPECT_EQ(route_2->output_interface(), "wlan0");
  EXPECT_EQ(route_2->table_id(), "1003");

  // Route with a certain destination prefix.
  const auto route_3 = Route::CreateFromRouteString(
      "fe80::/64 dev arc_ns0 proto kernel metric 256 pref medium"
      "100.115.92.133",
      net_base::IPFamily::kIPv6);
  ASSERT_TRUE(route_3.has_value());
  EXPECT_EQ(route_3->type(), Route::Type::kUnicast);
  EXPECT_EQ(route_3->destination_prefix(),
            net_base::IPCIDR::CreateFromCIDRString("fe80::/64").value());
  EXPECT_EQ(route_3->next_hop(), std::nullopt);
  EXPECT_EQ(route_3->output_interface(), "arc_ns0");
  EXPECT_EQ(route_3->table_id(), "main");

  // Test the special case (e.g. no prefix length in route strings).
  const auto route_with_no_prefix_length = Route::CreateFromRouteString(
      "local fe80:: dev arc_ns1 table local proto kernel scope host "
      "src 100.115.92.133",
      net_base::IPFamily::kIPv6);
  const auto prefix =
      net_base::IPCIDR::CreateFromCIDRString("fe80::/128").value();
  EXPECT_EQ(route_with_no_prefix_length.value().destination_prefix(), prefix);
}

// TODO(b/307460180): Add more cases (e.g. cases when a Route member exists
// without the corresponding identifier or when some identifiers exist
// adjacently).
TEST(RouteTest, CreateFromRouteStringFail) {
  // No valid value after an identifier.
  EXPECT_EQ(Route::CreateFromRouteString("192.25.25.0/24 dev",
                                         net_base::IPFamily::kIPv4),
            std::nullopt);
  EXPECT_EQ(Route::CreateFromRouteString("192.25.25.0/24 dev eth0 table",
                                         net_base::IPFamily::kIPv4),
            std::nullopt);
  EXPECT_EQ(Route::CreateFromRouteString("192.25.25.0/24 dev eth0 via",
                                         net_base::IPFamily::kIPv4),
            std::nullopt);
  // No destination prefix
  EXPECT_EQ(Route::CreateFromRouteString("via 100.87.84.254 dev eth0",
                                         net_base::IPFamily::kIPv4),
            std::nullopt);
  // Empty strings
  EXPECT_EQ(Route::CreateFromRouteString("", net_base::IPFamily::kIPv4),
            std::nullopt);
  // Input strings only contain route type.
  EXPECT_EQ(Route::CreateFromRouteString("local", net_base::IPFamily::kIPv4),
            std::nullopt);
}

TEST(RouteTest, CreateFromRouteStringHandleSpaceSuccess) {
  const auto prefix =
      net_base::IPCIDR::CreateFromCIDRString("192.25.25.0/24").value();
  std::string output_interface = "eth1";
  for (const auto route_str :
       {"192.25.25.0/24    dev eth1", "192.25.25.0/24 dev eth1  "}) {
    const auto route =
        Route::CreateFromRouteString(route_str, net_base::IPFamily::kIPv4);
    ASSERT_TRUE(route.has_value());
    EXPECT_EQ(route->destination_prefix(), prefix);
    EXPECT_EQ(route->output_interface(), output_interface);
  }
}

}  // namespace
}  // namespace routing_simulator
