// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "routing-simulator/routing_policy_entry.h"

#include <gtest/gtest.h>
#include <net-base/ip_address.h>

namespace routing_simulator {
namespace {

constexpr auto kIPv4DefaultPrefix = net_base::IPCIDR(net_base::IPFamily::kIPv4);
constexpr auto kIPv6DefaultPrefix = net_base::IPCIDR(net_base::IPFamily::kIPv6);

TEST(RoutingPolicyEntryTest, CreateFromPolicyStringIPv4Success) {
  // Policy with fwmark.
  const auto policy_1 = RoutingPolicyEntry::CreateFromPolicyString(
      "1020: from all fwmark 0x3eb0000/0xffff0000 lookup 1003",
      net_base::IPFamily::kIPv4);
  ASSERT_TRUE(policy_1.has_value());
  EXPECT_EQ(policy_1->priority(), 1020);
  EXPECT_EQ(policy_1->source_prefix(), kIPv4DefaultPrefix);
  EXPECT_EQ(policy_1->table_id(), "1003");
  EXPECT_EQ(policy_1->output_interface(), "");
  EXPECT_EQ(policy_1->input_interface(), "");
  EXPECT_EQ(policy_1->fwmark(), "0x3eb0000/0xffff0000");

  // Policy with input interface.
  const auto policy_2 = RoutingPolicyEntry::CreateFromPolicyString(
      "1020: from all iif wlan0 lookup 1003", net_base::IPFamily::kIPv4);
  ASSERT_TRUE(policy_2.has_value());
  EXPECT_EQ(policy_2->priority(), 1020);
  EXPECT_EQ(policy_2->source_prefix(), kIPv4DefaultPrefix);
  EXPECT_EQ(policy_2->table_id(), "1003");
  EXPECT_EQ(policy_2->output_interface(), "");
  EXPECT_EQ(policy_2->input_interface(), "wlan0");
  EXPECT_EQ(policy_2->fwmark(), "");

  // Policy with output interface.
  const auto policy_3 = RoutingPolicyEntry::CreateFromPolicyString(
      "1020: from all oif wlan0 lookup main", net_base::IPFamily::kIPv4);
  ASSERT_TRUE(policy_3.has_value());
  EXPECT_EQ(policy_3->priority(), 1020);
  EXPECT_EQ(policy_3->source_prefix(), kIPv4DefaultPrefix);
  EXPECT_EQ(policy_3->table_id(), "main");
  EXPECT_EQ(policy_3->output_interface(), "wlan0");
  EXPECT_EQ(policy_3->input_interface(), "");
  EXPECT_EQ(policy_3->fwmark(), "");

  // Policy with a certain destination prefix.
  const auto policy_4 = RoutingPolicyEntry::CreateFromPolicyString(
      "1020: from 100.86.210.153/22 lookup 1003", net_base::IPFamily::kIPv4);
  ASSERT_TRUE(policy_4.has_value());
  EXPECT_EQ(policy_4->priority(), 1020);
  EXPECT_EQ(policy_4->source_prefix(),
            net_base::IPCIDR::CreateFromCIDRString("100.86.210.153/22"));
  EXPECT_EQ(policy_4->table_id(), "1003");
  EXPECT_EQ(policy_4->output_interface(), "");
  EXPECT_EQ(policy_4->input_interface(), "");
  EXPECT_EQ(policy_4->fwmark(), "");

  // Test the special case (e.g. no prefix length in route strings).
  const auto route_with_no_prefix_length =
      RoutingPolicyEntry::CreateFromPolicyString(
          "1020: from 100.86.210.153 lookup 1003", net_base::IPFamily::kIPv4);
  const auto prefix =
      net_base::IPCIDR::CreateFromCIDRString("100.86.210.153").value();
  ASSERT_TRUE(route_with_no_prefix_length.has_value());
  EXPECT_EQ(route_with_no_prefix_length->source_prefix(), prefix);
}

TEST(RoutingPolicyEntryTest, CreateFromPolicyStringIPv6Success) {
  // Policy with fwmark.
  const auto policy_1 = RoutingPolicyEntry::CreateFromPolicyString(
      "1010: from all fwmark 0x3ea0000/0xffff0000 lookup 1002",
      net_base::IPFamily::kIPv6);
  ASSERT_TRUE(policy_1.has_value());
  EXPECT_EQ(policy_1->priority(), 1010);
  EXPECT_EQ(policy_1->source_prefix(), kIPv6DefaultPrefix);
  EXPECT_EQ(policy_1->table_id(), "1002");
  EXPECT_EQ(policy_1->output_interface(), "");
  EXPECT_EQ(policy_1->input_interface(), "");
  EXPECT_EQ(policy_1->fwmark(), "0x3ea0000/0xffff0000");

  // Policy with a output interface.
  const auto policy_2 = RoutingPolicyEntry::CreateFromPolicyString(
      "1010: from all oif eth0 lookup 1002", net_base::IPFamily::kIPv6);
  ASSERT_TRUE(policy_2.has_value());
  EXPECT_EQ(policy_2->priority(), 1010);
  EXPECT_EQ(policy_2->source_prefix(), kIPv6DefaultPrefix);
  EXPECT_EQ(policy_2->table_id(), "1002");
  EXPECT_EQ(policy_2->output_interface(), "eth0");
  EXPECT_EQ(policy_2->input_interface(), "");
  EXPECT_EQ(policy_2->fwmark(), "");

  // Policy with a certain destination prefix.
  const auto policy_3 = RoutingPolicyEntry::CreateFromPolicyString(
      "1010: from 2401:fa00:480:ee08:7022:5d3a:3805:7110/64 lookup main",
      net_base::IPFamily::kIPv6);
  ASSERT_TRUE(policy_3.has_value());
  EXPECT_EQ(policy_3->priority(), 1010);
  EXPECT_EQ(policy_3->source_prefix(),
            net_base::IPCIDR::CreateFromCIDRString(
                "2401:fa00:480:ee08:7022:5d3a:3805:7110/64"));
  EXPECT_EQ(policy_3->table_id(), "main");
  EXPECT_EQ(policy_3->output_interface(), "");
  EXPECT_EQ(policy_3->input_interface(), "");
  EXPECT_EQ(policy_3->fwmark(), "");

  // Policy with input interface.
  const auto policy_4 = RoutingPolicyEntry::CreateFromPolicyString(
      "1010: from all iif eth0 lookup 1002", net_base::IPFamily::kIPv6);
  ASSERT_TRUE(policy_4.has_value());
  EXPECT_EQ(policy_4->priority(), 1010);
  EXPECT_EQ(policy_4->source_prefix(), kIPv6DefaultPrefix);
  EXPECT_EQ(policy_4->table_id(), "1002");
  EXPECT_EQ(policy_4->output_interface(), "");
  EXPECT_EQ(policy_4->input_interface(), "eth0");
  EXPECT_EQ(policy_4->fwmark(), "");

  // Test the special case (e.g. no prefix length in route strings).
  // Test the special case (e.g. no prefix length in route strings).
  const auto route_with_no_prefix_length =
      RoutingPolicyEntry::CreateFromPolicyString(
          "1020: from 2401:fa00:480:ee08:7022:5d3a:3805:7110 lookup 1003",
          net_base::IPFamily::kIPv6);
  const auto prefix = net_base::IPCIDR::CreateFromCIDRString(
                          "2401:fa00:480:ee08:7022:5d3a:3805:7110")
                          .value();
  ASSERT_TRUE(route_with_no_prefix_length.has_value());
  EXPECT_EQ(route_with_no_prefix_length->source_prefix(), prefix);
}

TEST(RoutingPolicyEntryTest, CreateFromPolicyStringFail) {
  // Priority is out of the valid range(0~32767).
  EXPECT_EQ(RoutingPolicyEntry::CreateFromPolicyString(
                "32800: from all fwmark 0x3eb0000/0xffff0000 lookup 1003",
                net_base::IPFamily::kIPv4),
            std::nullopt);

  // No source prefix identifier.
  EXPECT_EQ(RoutingPolicyEntry::CreateFromPolicyString(
                "1002: fwmark 0x3eb0000/0xffff0000 lookup 1003",
                net_base::IPFamily::kIPv4),
            std::nullopt);

  // No table id identifier.
  EXPECT_EQ(RoutingPolicyEntry::CreateFromPolicyString(
                "1002: from all fwmark 0x3eb0000/0xffff0000 1003",
                net_base::IPFamily::kIPv4),
            std::nullopt);

  // Identifiers next to each other.
  EXPECT_EQ(RoutingPolicyEntry::CreateFromPolicyString(
                "1002: from all fwmark 0x3eb0000/0xffff0000 table lookup 1003",
                net_base::IPFamily::kIPv4),
            std::nullopt);

  // Invalid prefix.
  EXPECT_EQ(RoutingPolicyEntry::CreateFromPolicyString(
                "1020: from default lookup 1003", net_base::IPFamily::kIPv4),
            std::nullopt);
}

}  // namespace
}  // namespace routing_simulator
