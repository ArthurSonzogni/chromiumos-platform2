// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <linux/fib_rules.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <net-base/ip_address.h>
#include <net-base/mock_proc_fs_stub.h>
#include <net-base/mock_rtnl_handler.h>
#include <net-base/network_priority.h>

#include "shill/network/mock_network_applier.h"
#include "shill/network/mock_routing_policy_service.h"
#include "shill/network/mock_routing_table.h"
#include "shill/network/network_applier.h"
#include "shill/network/routing_table.h"
#include "shill/technology.h"

using testing::_;
using testing::Eq;
using testing::Mock;
using testing::Ne;
using testing::Return;
using testing::ReturnRef;
using testing::StrictMock;
using testing::Test;

namespace shill {

namespace {
MATCHER_P3(IsValidRoutingRule, family, priority, table, "") {
  return arg.family == family && arg.priority == priority && arg.table == table;
}

MATCHER_P4(IsValidFwMarkRule, family, priority, fwmark, table, "") {
  return arg.family == family && arg.priority == priority &&
         arg.fw_mark == fwmark && arg.table == table;
}

MATCHER_P4(IsValidIifRule, family, priority, iif, table, "") {
  return arg.family == family && arg.priority == priority &&
         arg.iif_name == iif && arg.table == table;
}

MATCHER_P4(IsValidOifRule, family, priority, oif, table, "") {
  return arg.family == family && arg.priority == priority &&
         arg.oif_name == oif && arg.table == table;
}

MATCHER_P4(IsValidSrcRule, family, priority, src, table, "") {
  return arg.family == family && arg.priority == priority && arg.src == src &&
         arg.table == table;
}

MATCHER_P4(IsValidDstRule, family, priority, dst, table, "") {
  return arg.family == family && arg.priority == priority && arg.dst == dst &&
         arg.table == table;
}

MATCHER_P4(IsValidUidRule, family, priority, uid, table, "") {
  return arg.family == family && arg.priority == priority && arg.uid_range &&
         arg.uid_range->start == uid && arg.uid_range->end == uid &&
         arg.table == table;
}

MATCHER_P5(IsValidFwMarkRuleWithUid, family, priority, fwmark, uid, table, "") {
  return arg.family == family && arg.priority == priority &&
         arg.fw_mark == fwmark && arg.uid_range &&
         arg.uid_range->start == uid && arg.uid_range->end == uid &&
         arg.table == table;
}

MATCHER_P5(IsValidIifRuleWithUid, family, priority, iif, uid, table, "") {
  return arg.family == family && arg.priority == priority &&
         arg.iif_name == iif && arg.uid_range && arg.uid_range->start == uid &&
         arg.uid_range->end == uid && arg.table == table;
}

MATCHER_P5(IsValidOifRuleWithUid, family, priority, oif, uid, table, "") {
  return arg.family == family && arg.priority == priority &&
         arg.oif_name == oif && arg.uid_range && arg.uid_range->start == uid &&
         arg.uid_range->end == uid && arg.table == table;
}

MATCHER_P5(IsValidSrcRuleWithUid, family, priority, src, uid, table, "") {
  return arg.family == family && arg.priority == priority && arg.src == src &&
         arg.uid_range && arg.uid_range->start == uid &&
         arg.uid_range->end == uid && arg.table == table;
}

MATCHER_P(IsValidRoute, dst, "") {
  return dst == arg.dst;
}

MATCHER_P2(IsValidRouteThrough, dst, gateway, "") {
  return dst == arg.dst && gateway == arg.gateway;
}

MATCHER_P(IsValidThrowRoute, dst, "") {
  return dst == arg.dst && arg.type == RTN_THROW;
}

MATCHER_P(IsLinkRouteTo, dst, "") {
  return dst == arg.dst && arg.src.address().IsZero() && arg.gateway.IsZero() &&
         arg.scope == RT_SCOPE_LINK;
}

}  // namespace

class NetworkApplierTest : public Test {
 public:
  NetworkApplierTest() {
    auto temp_proc_fs_ptr = std::make_unique<net_base::MockProcFsStub>("");
    proc_fs_ = temp_proc_fs_ptr.get();
    auto temp_address_service_ptr =
        AddressService::CreateForTesting(&address_rtnl_handler_);
    address_service_ = temp_address_service_ptr.get();
    auto temp_routing_table_ptr =
        std::make_unique<StrictMock<MockRoutingTable>>();
    routing_table_ = temp_routing_table_ptr.get();
    auto temp_rule_table_ptr =
        std::make_unique<StrictMock<MockRoutingPolicyService>>();
    rule_table_ = temp_rule_table_ptr.get();

    network_applier_ = NetworkApplier::CreateForTesting(
        std::move(temp_routing_table_ptr), std::move(temp_rule_table_ptr),
        std::move(temp_address_service_ptr), &rtnl_handler_,
        std::move(temp_proc_fs_ptr));
  }

 protected:
  StrictMock<MockRoutingTable>* routing_table_;  // owned by network_applier_
  StrictMock<MockRoutingPolicyService>*
      rule_table_;  // owned by network_applier_
  StrictMock<net_base::MockRTNLHandler> address_rtnl_handler_;
  AddressService* address_service_;  // mocked at net_base::RTNLHandler level,
                                     // owned by network_applier_
  net_base::MockRTNLHandler rtnl_handler_;
  net_base::MockProcFsStub* proc_fs_;  // owned by network_applier_;
  std::unique_ptr<NetworkApplier> network_applier_;
};

TEST_F(NetworkApplierTest, ApplyNetworkConfig) {
  // Use a mocked NetworkApplier to test behavior at Apply*() layer.
  StrictMock<MockNetworkApplier> applier;
  const int kInterfaceIndex = 3;
  const std::string kInterfaceName = "placeholder";
  net_base::NetworkPriority priority;
  net_base::NetworkConfig config;
  config.ipv4_address =
      *net_base::IPv4CIDR::CreateFromCIDRString("192.0.2.100/24");
  config.ipv6_addresses = {
      *net_base::IPv6CIDR::CreateFromCIDRString("2001:db8:1::1000/64")};

  applier.ApplyNetworkConfig(kInterfaceIndex, kInterfaceName,
                             NetworkApplier::Area::kNone, config, priority,
                             Technology::kEthernet);

  EXPECT_CALL(applier, ApplyAddress(kInterfaceIndex, _, _)).Times(1);
  applier.ApplyNetworkConfig(kInterfaceIndex, kInterfaceName,
                             NetworkApplier::Area::kIPv4Address, config,
                             priority, Technology::kEthernet);

  EXPECT_CALL(applier, ApplyRoute(kInterfaceIndex, net_base::IPFamily::kIPv4,
                                  /*gateway=*/Eq(std::nullopt),
                                  /*fix_gateway_reachability=*/false,
                                  /*default_route=*/false,
                                  /*blackhole_ipv6=*/false, _, _, _))
      .Times(1);
  applier.ApplyNetworkConfig(kInterfaceIndex, kInterfaceName,
                             NetworkApplier::Area::kIPv4Route, config, priority,
                             Technology::kEthernet);

  EXPECT_CALL(applier, ApplyRoute(kInterfaceIndex, net_base::IPFamily::kIPv4,
                                  /*gateway=*/Eq(std::nullopt),
                                  /*fix_gateway_reachability=*/false,
                                  /*default_route=*/true,
                                  /*blackhole_ipv6=*/false, _, _, _))
      .Times(1);
  applier.ApplyNetworkConfig(kInterfaceIndex, kInterfaceName,
                             NetworkApplier::Area::kIPv4Route |
                                 NetworkApplier::Area::kIPv4DefaultRoute,
                             config, priority, Technology::kEthernet);

  EXPECT_CALL(applier, ApplyAddress(kInterfaceIndex, _, _)).Times(1);
  applier.ApplyNetworkConfig(kInterfaceIndex, kInterfaceName,
                             NetworkApplier::Area::kIPv6Address, config,
                             priority, Technology::kEthernet);

  EXPECT_CALL(applier, ApplyRoute(kInterfaceIndex, net_base::IPFamily::kIPv6,
                                  /*gateway=*/Eq(std::nullopt),
                                  /*fix_gateway_reachability=*/false,
                                  /*default_route=*/false,
                                  /*blackhole_ipv6=*/false, _, _, _))
      .Times(1);
  applier.ApplyNetworkConfig(kInterfaceIndex, kInterfaceName,
                             NetworkApplier::Area::kIPv6Route, config, priority,
                             Technology::kEthernet);

  EXPECT_CALL(applier, ApplyRoute(kInterfaceIndex, net_base::IPFamily::kIPv6,
                                  /*gateway=*/Eq(std::nullopt),
                                  /*fix_gateway_reachability=*/false,
                                  /*default_route=*/true,
                                  /*blackhole_ipv6=*/false, _, _, _))
      .Times(1);
  applier.ApplyNetworkConfig(kInterfaceIndex, kInterfaceName,
                             NetworkApplier::Area::kIPv6Route |
                                 NetworkApplier::Area::kIPv6DefaultRoute,
                             config, priority, Technology::kEthernet);

  EXPECT_CALL(applier,
              ApplyRoutingPolicy(kInterfaceIndex, kInterfaceName,
                                 Technology::kEthernet, priority, _, _));
  applier.ApplyNetworkConfig(kInterfaceIndex, kInterfaceName,
                             NetworkApplier::Area::kRoutingPolicy, config,
                             priority, Technology::kEthernet);

  EXPECT_CALL(applier, ApplyDNS(priority, _, _)).Times(1);
  applier.ApplyNetworkConfig(kInterfaceIndex, kInterfaceName,
                             NetworkApplier::Area::kDNS, config, priority,
                             Technology::kEthernet);

  config.mtu = 1480;
  EXPECT_CALL(applier, ApplyMTU(kInterfaceIndex, 1480)).Times(1);
  applier.ApplyNetworkConfig(kInterfaceIndex, kInterfaceName,
                             NetworkApplier::Area::kMTU, config, priority,
                             Technology::kEthernet);
}

TEST_F(NetworkApplierTest, ApplyNetworkConfigRouteParameters) {
  // Use a mocked NetworkApplier to test behavior at Apply*() layer.
  StrictMock<MockNetworkApplier> applier;
  const int kInterfaceIndex = 3;
  const std::string kInterfaceName = "placeholder";
  net_base::NetworkPriority priority;
  net_base::NetworkConfig config;
  config.ipv4_address =
      *net_base::IPv4CIDR::CreateFromCIDRString("192.0.2.100/24");
  config.ipv4_gateway = *net_base::IPv4Address::CreateFromString("192.0.2.1");

  EXPECT_CALL(applier, ApplyRoute(kInterfaceIndex, net_base::IPFamily::kIPv4,
                                  /*gateway=*/Ne(std::nullopt),
                                  /*fix_gateway_reachability=*/false,
                                  /*default_route=*/true,
                                  /*blackhole_ipv6=*/false, _, _, _))
      .Times(1);
  applier.ApplyNetworkConfig(kInterfaceIndex, kInterfaceName,
                             NetworkApplier::Area::kIPv4Route |
                                 NetworkApplier::Area::kIPv4DefaultRoute,
                             config, priority, Technology::kEthernet);

  config.ipv4_default_route = false;
  EXPECT_CALL(applier, ApplyRoute(kInterfaceIndex, net_base::IPFamily::kIPv4,
                                  /*gateway=*/Ne(std::nullopt),
                                  /*fix_gateway_reachability=*/false,
                                  /*default_route=*/false,
                                  /*blackhole_ipv6=*/false, _, _, _))
      .Times(1);
  applier.ApplyNetworkConfig(kInterfaceIndex, kInterfaceName,
                             NetworkApplier::Area::kIPv4Route |
                                 NetworkApplier::Area::kIPv4DefaultRoute,
                             config, priority, Technology::kEthernet);

  config.ipv4_default_route = true;
  config.ipv6_blackhole_route = true;
  EXPECT_CALL(applier, ApplyRoute(kInterfaceIndex, net_base::IPFamily::kIPv4,
                                  /*gateway=*/Ne(std::nullopt),
                                  /*fix_gateway_reachability=*/false,
                                  /*default_route=*/true,
                                  /*blackhole_ipv6=*/true, _, _, _))
      .Times(1);
  applier.ApplyNetworkConfig(kInterfaceIndex, kInterfaceName,
                             NetworkApplier::Area::kIPv4Route |
                                 NetworkApplier::Area::kIPv4DefaultRoute,
                             config, priority, Technology::kEthernet);

  config.ipv4_gateway = *net_base::IPv4Address::CreateFromString(
      "198.51.100.1");  // Out of 192.0.2.100/24
  EXPECT_CALL(applier, ApplyRoute(kInterfaceIndex, net_base::IPFamily::kIPv4,
                                  /*gateway=*/Ne(std::nullopt),
                                  /*fix_gateway_reachability=*/true,
                                  /*default_route=*/true,
                                  /*blackhole_ipv6=*/true, _, _, _))
      .Times(1);
  applier.ApplyNetworkConfig(kInterfaceIndex, kInterfaceName,
                             NetworkApplier::Area::kIPv4Route |
                                 NetworkApplier::Area::kIPv4DefaultRoute,
                             config, priority, Technology::kEthernet);
}

TEST_F(NetworkApplierTest, ApplyNetworkConfigRoutingPolicyParameters) {
  // Use a mocked NetworkApplier to test behavior at Apply*() layer.
  StrictMock<MockNetworkApplier> applier;
  const int kInterfaceIndex = 3;
  const std::string kInterfaceName = "placeholder";
  net_base::NetworkPriority priority;
  net_base::NetworkConfig config;
  config.ipv4_address =
      *net_base::IPv4CIDR::CreateFromCIDRString("192.0.2.100/24");
  config.ipv6_addresses = {
      *net_base::IPv6CIDR::CreateFromCIDRString("2001:db8:1::1000/64"),
      *net_base::IPv6CIDR::CreateFromCIDRString("2001:db8:1::2000/64")};
  config.rfc3442_routes = {
      {*net_base::IPv4CIDR::CreateFromCIDRString("203.0.113.100/32"),
       *net_base::IPv4Address::CreateFromString("203.0.113.1")},
      {*net_base::IPv4CIDR::CreateFromCIDRString("203.0.113.128/25"),
       *net_base::IPv4Address::CreateFromString("203.0.113.2")}};

  EXPECT_CALL(
      applier,
      ApplyRoutingPolicy(
          kInterfaceIndex, kInterfaceName, Technology::kEthernet, priority,
          Eq(std::vector<net_base::IPCIDR>{
              *net_base::IPCIDR::CreateFromCIDRString("192.0.2.100/24"),
              *net_base::IPCIDR::CreateFromCIDRString("2001:db8:1::1000/64"),
              *net_base::IPCIDR::CreateFromCIDRString("2001:db8:1::2000/64")}),
          Eq(std::vector<net_base::IPv4CIDR>{
              *net_base::IPv4CIDR::CreateFromCIDRString("203.0.113.100/32"),
              *net_base::IPv4CIDR::CreateFromCIDRString("203.0.113.128/25")})));
  applier.ApplyNetworkConfig(kInterfaceIndex, kInterfaceName,
                             NetworkApplier::Area::kRoutingPolicy, config,
                             priority, Technology::kEthernet);
}

using NetworkApplierRoutingPolicyTest = NetworkApplierTest;

TEST_F(NetworkApplierRoutingPolicyTest, DefaultPhysical) {
  const int kInterfaceIndex = 3;
  const std::string kInterfaceName = "eth0";

  net_base::NetworkPriority priority;
  priority.is_primary_physical = true;
  priority.is_primary_logical = true;
  priority.ranking_order = 0;

  auto all_addresses = std::vector<net_base::IPCIDR>{
      *net_base::IPCIDR::CreateFromStringAndPrefix("198.51.100.101", 24),
      *net_base::IPCIDR::CreateFromStringAndPrefix("2001:db8:0:1000::abcd",
                                                   64)};

  RoutingPolicyEntry::FwMark routing_fwmark;
  routing_fwmark.value = (1000 + kInterfaceIndex) << 16;
  routing_fwmark.mask = 0xffff0000;
  const uint32_t kExpectedTable = 1003u;

  EXPECT_CALL(*rule_table_, FlushRules(kInterfaceIndex));

  // IPv4 rules:
  // 1000:  from all lookup main
  EXPECT_CALL(*rule_table_,
              AddRule(-1, IsValidRoutingRule(net_base::IPFamily::kIPv4, 1000u,
                                             RT_TABLE_MAIN)))
      .WillOnce(Return(true));
  // 1010:  from all fwmark 0x3eb0000/0xffff0000 lookup 1003
  EXPECT_CALL(*rule_table_,
              AddRule(kInterfaceIndex,
                      IsValidFwMarkRule(net_base::IPFamily::kIPv4, 1010u,
                                        routing_fwmark, kExpectedTable)))
      .WillOnce(Return(true));
  // 1010:  from all oif eth0 lookup 1003
  EXPECT_CALL(
      *rule_table_,
      AddRule(kInterfaceIndex, IsValidOifRule(net_base::IPFamily::kIPv4, 1010u,
                                              "eth0", kExpectedTable)))
      .WillOnce(Return(true));
  // 1010:  from 198.51.100.101/24 lookup 1003
  EXPECT_CALL(*rule_table_,
              AddRule(kInterfaceIndex,
                      IsValidSrcRule(net_base::IPFamily::kIPv4, 1010u,
                                     all_addresses[0], kExpectedTable)))
      .WillOnce(Return(true));
  // 1010:  from all iif eth0 lookup 1003
  EXPECT_CALL(
      *rule_table_,
      AddRule(kInterfaceIndex, IsValidIifRule(net_base::IPFamily::kIPv4, 1010u,
                                              "eth0", kExpectedTable)))
      .WillOnce(Return(true));
  // 32763: from 100.115.92.24/29 lookup 249
  // 32763: from 100.115.92.32/27 lookup 249
  // 32763: from 100.115.92.64/26 lookup 249
  // 32763: from 100.115.92.192/26 lookup 249
  EXPECT_CALL(
      *rule_table_,
      AddRule(-1, IsValidSrcRule(net_base::IPFamily::kIPv4, 32763u,
                                 *net_base::IPCIDR::CreateFromCIDRString(
                                     "100.115.92.24/29"),
                                 249u)));
  EXPECT_CALL(
      *rule_table_,
      AddRule(-1, IsValidSrcRule(net_base::IPFamily::kIPv4, 32763u,
                                 *net_base::IPCIDR::CreateFromCIDRString(
                                     "100.115.92.32/27"),
                                 249u)));
  EXPECT_CALL(
      *rule_table_,
      AddRule(-1, IsValidSrcRule(net_base::IPFamily::kIPv4, 32763u,
                                 *net_base::IPCIDR::CreateFromCIDRString(
                                     "100.115.92.64/26"),
                                 249u)));
  EXPECT_CALL(
      *rule_table_,
      AddRule(-1, IsValidSrcRule(net_base::IPFamily::kIPv4, 32763u,
                                 *net_base::IPCIDR::CreateFromCIDRString(
                                     "100.115.92.192/26"),
                                 249u)));
  // 32765: from all lookup 1003
  EXPECT_CALL(
      *rule_table_,
      AddRule(kInterfaceIndex, IsValidRoutingRule(net_base::IPFamily::kIPv4,
                                                  32765u, kExpectedTable)))
      .WillOnce(Return(true));

  // IPv6 rules:
  // 1000:  from all lookup main
  EXPECT_CALL(*rule_table_,
              AddRule(-1, IsValidRoutingRule(net_base::IPFamily::kIPv6, 1000u,
                                             RT_TABLE_MAIN)))
      .WillOnce(Return(true));
  // 1010:  from all fwmark 0x3eb0000/0xffff0000 lookup 1003
  EXPECT_CALL(*rule_table_,
              AddRule(kInterfaceIndex,
                      IsValidFwMarkRule(net_base::IPFamily::kIPv6, 1010u,
                                        routing_fwmark, kExpectedTable)))
      .WillOnce(Return(true));
  // 1010:  from all oif eth0 lookup 1003
  EXPECT_CALL(
      *rule_table_,
      AddRule(kInterfaceIndex, IsValidOifRule(net_base::IPFamily::kIPv6, 1010u,
                                              "eth0", kExpectedTable)))
      .WillOnce(Return(true));
  // 1010:  from 2001:db8:0:1000::abcd/64 lookup 1003
  EXPECT_CALL(*rule_table_,
              AddRule(kInterfaceIndex,
                      IsValidSrcRule(net_base::IPFamily::kIPv6, 1010u,
                                     all_addresses[1], kExpectedTable)))
      .WillOnce(Return(true));
  // 1010:  from all iif eth0 lookup 1003
  EXPECT_CALL(
      *rule_table_,
      AddRule(kInterfaceIndex, IsValidIifRule(net_base::IPFamily::kIPv6, 1010u,
                                              "eth0", kExpectedTable)))
      .WillOnce(Return(true));
  // 32765: from all lookup 1003
  EXPECT_CALL(
      *rule_table_,
      AddRule(kInterfaceIndex, IsValidRoutingRule(net_base::IPFamily::kIPv6,
                                                  32765u, kExpectedTable)))
      .WillOnce(Return(true));

  EXPECT_CALL(*proc_fs_, FlushRoutingCache()).WillOnce(Return(true));
  network_applier_->ApplyRoutingPolicy(
      kInterfaceIndex, kInterfaceName, Technology::kEthernet, priority,
      all_addresses, std::vector<net_base::IPv4CIDR>());
}

TEST_F(NetworkApplierRoutingPolicyTest, DefaultVPN) {
  const int kInterfaceIndex = 11;
  const std::string kInterfaceName = "tun0";

  net_base::NetworkPriority priority;
  priority.is_primary_logical = true;
  priority.ranking_order = 0;

  auto all_addresses = std::vector<net_base::IPCIDR>{
      *net_base::IPCIDR::CreateFromStringAndPrefix("198.51.100.101", 24),
      *net_base::IPCIDR::CreateFromStringAndPrefix("2001:db8:0:1000::abcd",
                                                   64)};

  RoutingPolicyEntry::FwMark routing_fwmark;
  routing_fwmark.value = (1000 + kInterfaceIndex) << 16;
  routing_fwmark.mask = 0xffff0000;
  const uint32_t kExpectedTable = 1011u;

  auto user_uids = base::flat_map<std::string_view, fib_rule_uid_range>{
      {"chronos", fib_rule_uid_range{100u, 100u}}};
  EXPECT_CALL(*rule_table_, GetUserTrafficUids())
      .WillOnce(ReturnRef(user_uids));

  EXPECT_CALL(*rule_table_, FlushRules(kInterfaceIndex));

  // IPv4 rules:
  // 10:    from all fwmark 0x3f30000/0xffff0000 lookup 1011
  EXPECT_CALL(*rule_table_,
              AddRule(kInterfaceIndex,
                      IsValidFwMarkRule(net_base::IPFamily::kIPv4, 10u,
                                        routing_fwmark, kExpectedTable)))
      .WillOnce(Return(true));
  // 10:    from all oif tun0 lookup 1011
  EXPECT_CALL(
      *rule_table_,
      AddRule(kInterfaceIndex, IsValidOifRule(net_base::IPFamily::kIPv4, 10u,
                                              "tun0", kExpectedTable)))
      .WillOnce(Return(true));
  // 32763: from 100.115.92.24/29 lookup 249
  // 32763: from 100.115.92.32/27 lookup 249
  // 32763: from 100.115.92.64/26 lookup 249
  // 32763: from 100.115.92.192/26 lookup 249
  EXPECT_CALL(
      *rule_table_,
      AddRule(-1, IsValidSrcRule(net_base::IPFamily::kIPv4, 32763u,
                                 *net_base::IPCIDR::CreateFromCIDRString(
                                     "100.115.92.24/29"),
                                 249u)));
  EXPECT_CALL(
      *rule_table_,
      AddRule(-1, IsValidSrcRule(net_base::IPFamily::kIPv4, 32763u,
                                 *net_base::IPCIDR::CreateFromCIDRString(
                                     "100.115.92.32/27"),
                                 249u)));
  EXPECT_CALL(
      *rule_table_,
      AddRule(-1, IsValidSrcRule(net_base::IPFamily::kIPv4, 32763u,
                                 *net_base::IPCIDR::CreateFromCIDRString(
                                     "100.115.92.64/26"),
                                 249u)));
  EXPECT_CALL(
      *rule_table_,
      AddRule(-1, IsValidSrcRule(net_base::IPFamily::kIPv4, 32763u,
                                 *net_base::IPCIDR::CreateFromCIDRString(
                                     "100.115.92.192/26"),
                                 249u)));
  // 32764:  from all uidrange ()-() lookup 1003
  EXPECT_CALL(
      *rule_table_,
      AddRule(kInterfaceIndex, IsValidUidRule(net_base::IPFamily::kIPv4, 32764u,
                                              100u, kExpectedTable)))
      .WillOnce(Return(true));

  // IPv6 rules:
  // 10:    from all fwmark 0x3f30000/0xffff0000 lookup 1011
  EXPECT_CALL(*rule_table_,
              AddRule(kInterfaceIndex,
                      IsValidFwMarkRule(net_base::IPFamily::kIPv6, 10u,
                                        routing_fwmark, kExpectedTable)))
      .WillOnce(Return(true));
  // 10:    from all oif tun0 lookup 1011
  EXPECT_CALL(
      *rule_table_,
      AddRule(kInterfaceIndex, IsValidOifRule(net_base::IPFamily::kIPv6, 10u,
                                              "tun0", kExpectedTable)))
      .WillOnce(Return(true));
  // 32764:  from all uidrange ()-() lookup 1003
  EXPECT_CALL(
      *rule_table_,
      AddRule(kInterfaceIndex, IsValidUidRule(net_base::IPFamily::kIPv6, 32764u,
                                              100u, kExpectedTable)))
      .WillOnce(Return(true));

  EXPECT_CALL(*proc_fs_, FlushRoutingCache()).WillOnce(Return(true));
  network_applier_->ApplyRoutingPolicy(
      kInterfaceIndex, kInterfaceName, Technology::kVPN, priority,
      all_addresses, std::vector<net_base::IPv4CIDR>());
}

TEST_F(NetworkApplierRoutingPolicyTest,
       ApplyRoutingPolicy_NonDefaultPhysicalWithClasslessStaticRoute) {
  const int kInterfaceIndex = 4;
  const std::string kInterfaceName = "wlan0";

  net_base::NetworkPriority priority;
  priority.ranking_order = 1;

  auto all_addresses = std::vector<net_base::IPCIDR>{
      *net_base::IPCIDR::CreateFromStringAndPrefix("198.51.100.101", 24),
      *net_base::IPCIDR::CreateFromStringAndPrefix("2001:db8:0:1000::abcd",
                                                   64)};
  auto rfc3442_dsts = std::vector<net_base::IPv4CIDR>{
      *net_base::IPv4CIDR::CreateFromStringAndPrefix("203.0.113.0", 26),
      *net_base::IPv4CIDR::CreateFromStringAndPrefix("203.0.113.128", 26),
  };

  RoutingPolicyEntry::FwMark routing_fwmark;
  routing_fwmark.value = (1000 + kInterfaceIndex) << 16;
  routing_fwmark.mask = 0xffff0000;
  const uint32_t kExpectedTable = 1004u;

  EXPECT_CALL(*rule_table_, FlushRules(kInterfaceIndex));

  // IPv4 rules:
  // 1020:  from all fwmark 0x3ec0000/0xffff0000 lookup 1004
  EXPECT_CALL(*rule_table_,
              AddRule(kInterfaceIndex,
                      IsValidFwMarkRule(net_base::IPFamily::kIPv4, 1020u,
                                        routing_fwmark, kExpectedTable)))
      .WillOnce(Return(true));
  // 1020:  from all oif wlan0 lookup 1004
  EXPECT_CALL(
      *rule_table_,
      AddRule(kInterfaceIndex, IsValidOifRule(net_base::IPFamily::kIPv4, 1020u,
                                              kInterfaceName, kExpectedTable)))
      .WillOnce(Return(true));
  // 1020:  from 198.51.100.101/24 lookup 1004
  EXPECT_CALL(*rule_table_,
              AddRule(kInterfaceIndex,
                      IsValidSrcRule(net_base::IPFamily::kIPv4, 1020u,
                                     all_addresses[0], kExpectedTable)))
      .WillOnce(Return(true));
  // 1020:  from all iif wlan0 lookup 1004
  EXPECT_CALL(
      *rule_table_,
      AddRule(kInterfaceIndex, IsValidIifRule(net_base::IPFamily::kIPv4, 1020u,
                                              kInterfaceName, kExpectedTable)))
      .WillOnce(Return(true));
  // 32762:  from all to 203.0.113.0/26 lookup 1004
  // 32762:  from all to 203.0.113.128/26 lookup 1004
  EXPECT_CALL(
      *rule_table_,
      AddRule(kInterfaceIndex, IsValidDstRule(net_base::IPFamily::kIPv4, 32762u,
                                              net_base::IPCIDR(rfc3442_dsts[0]),
                                              kExpectedTable)))
      .WillOnce(Return(true));
  EXPECT_CALL(
      *rule_table_,
      AddRule(kInterfaceIndex, IsValidDstRule(net_base::IPFamily::kIPv4, 32762u,
                                              net_base::IPCIDR(rfc3442_dsts[1]),
                                              kExpectedTable)))
      .WillOnce(Return(true));

  // IPv6 rules:
  // 1020:  from all fwmark 0x3ec0000/0xffff0000 lookup 1004
  EXPECT_CALL(*rule_table_,
              AddRule(kInterfaceIndex,
                      IsValidFwMarkRule(net_base::IPFamily::kIPv6, 1020u,
                                        routing_fwmark, kExpectedTable)))
      .WillOnce(Return(true));
  // 1020:  from all oif wlan0 lookup 1004
  EXPECT_CALL(
      *rule_table_,
      AddRule(kInterfaceIndex, IsValidOifRule(net_base::IPFamily::kIPv6, 1020u,
                                              kInterfaceName, kExpectedTable)))
      .WillOnce(Return(true));
  // 1020:  from 198.51.100.101/24 lookup 1004
  EXPECT_CALL(*rule_table_,
              AddRule(kInterfaceIndex,
                      IsValidSrcRule(net_base::IPFamily::kIPv6, 1020u,
                                     all_addresses[1], kExpectedTable)))
      .WillOnce(Return(true));
  // 1020:  from all iif wlan0 lookup 1004
  EXPECT_CALL(
      *rule_table_,
      AddRule(kInterfaceIndex, IsValidIifRule(net_base::IPFamily::kIPv6, 1020u,
                                              kInterfaceName, kExpectedTable)))
      .WillOnce(Return(true));

  EXPECT_CALL(*proc_fs_, FlushRoutingCache()).WillOnce(Return(true));
  network_applier_->ApplyRoutingPolicy(kInterfaceIndex, kInterfaceName,
                                       Technology::kWiFi, priority,
                                       all_addresses, rfc3442_dsts);
}

TEST_F(NetworkApplierRoutingPolicyTest,
       NonDefaultCellularShouldHaveChromeIPv6Blocked) {
  const int kInterfaceIndex = 5;
  const std::string kInterfaceName = "wwan0";

  net_base::NetworkPriority priority;
  priority.ranking_order = 2;

  auto all_addresses = std::vector<net_base::IPCIDR>{
      *net_base::IPCIDR::CreateFromStringAndPrefix("198.51.100.101", 24),
      *net_base::IPCIDR::CreateFromStringAndPrefix("2001:db8:0:1000::abcd",
                                                   64)};

  RoutingPolicyEntry::FwMark routing_fwmark;
  routing_fwmark.value = (1000 + kInterfaceIndex) << 16;
  routing_fwmark.mask = 0xffff0000;
  const uint32_t kExpectedTable = 1005u;

  EXPECT_CALL(*rule_table_, GetChromeUid())
      .WillOnce(Return(fib_rule_uid_range{100u, 100u}));
  EXPECT_CALL(*rule_table_, FlushRules(kInterfaceIndex));

  // IPv4 rules:
  // 1030:  from all fwmark 0x3ed0000/0xffff0000 lookup 1005
  EXPECT_CALL(*rule_table_,
              AddRule(kInterfaceIndex,
                      IsValidFwMarkRule(net_base::IPFamily::kIPv4, 1030u,
                                        routing_fwmark, kExpectedTable)))
      .WillOnce(Return(true));
  // 1030:  from all oif wwan0 lookup 1005
  EXPECT_CALL(
      *rule_table_,
      AddRule(kInterfaceIndex, IsValidOifRule(net_base::IPFamily::kIPv4, 1030u,
                                              kInterfaceName, kExpectedTable)))
      .WillOnce(Return(true));
  // 1030:  from 198.51.100.101/24 lookup 1005
  EXPECT_CALL(*rule_table_,
              AddRule(kInterfaceIndex,
                      IsValidSrcRule(net_base::IPFamily::kIPv4, 1030u,
                                     all_addresses[0], kExpectedTable)))
      .WillOnce(Return(true));
  // 1030:  from all iif wwan0 lookup 1005
  EXPECT_CALL(
      *rule_table_,
      AddRule(kInterfaceIndex, IsValidIifRule(net_base::IPFamily::kIPv4, 1030u,
                                              kInterfaceName, kExpectedTable)))
      .WillOnce(Return(true));

  // IPv6 rules:
  // 1029:  from 2001:db8:0:1000::abcd/64 uidrange (chrome) lookup 250
  EXPECT_CALL(*rule_table_,
              AddRule(kInterfaceIndex,
                      IsValidSrcRuleWithUid(net_base::IPFamily::kIPv6, 1029u,
                                            all_addresses[1], 100u,
                                            RoutingTable::kUnreachableTableId)))
      .WillOnce(Return(true));
  // 1030:  from all fwmark 0x3ed0000/0xffff0000 lookup 1005
  EXPECT_CALL(*rule_table_,
              AddRule(kInterfaceIndex,
                      IsValidFwMarkRule(net_base::IPFamily::kIPv6, 1030u,
                                        routing_fwmark, kExpectedTable)))
      .WillOnce(Return(true));
  // 1030:  from all oif wlan0 lookup 1005
  EXPECT_CALL(
      *rule_table_,
      AddRule(kInterfaceIndex, IsValidOifRule(net_base::IPFamily::kIPv6, 1030u,
                                              kInterfaceName, kExpectedTable)))
      .WillOnce(Return(true));
  // 1030:  from 2001:db8:0:1000::abcd/64 lookup 1005
  EXPECT_CALL(*rule_table_,
              AddRule(kInterfaceIndex,
                      IsValidSrcRule(net_base::IPFamily::kIPv6, 1030u,
                                     all_addresses[1], kExpectedTable)))
      .WillOnce(Return(true));
  // 1030:  from all iif wwan0 lookup 1005
  EXPECT_CALL(
      *rule_table_,
      AddRule(kInterfaceIndex, IsValidIifRule(net_base::IPFamily::kIPv6, 1030u,
                                              kInterfaceName, kExpectedTable)))
      .WillOnce(Return(true));

  EXPECT_CALL(*proc_fs_, FlushRoutingCache()).WillOnce(Return(true));
  network_applier_->ApplyRoutingPolicy(
      kInterfaceIndex, kInterfaceName, Technology::kCellular, priority,
      all_addresses, std::vector<net_base::IPv4CIDR>());
}

using NetworkApplierRouteTest = NetworkApplierTest;

TEST_F(NetworkApplierRouteTest, IPv4Simple) {
  const int kInterfaceIndex = 3;
  const int kTableID = 1003;
  const auto gateway = net_base::IPAddress::CreateFromString("192.168.1.1");

  EXPECT_CALL(*routing_table_,
              FlushRoutesWithTag(kInterfaceIndex, net_base::IPFamily::kIPv4));
  EXPECT_CALL(*routing_table_,
              SetDefaultRoute(kInterfaceIndex, *gateway, kTableID))
      .WillOnce(Return(true));
  EXPECT_CALL(*routing_table_,
              CreateBlackholeRoute(kInterfaceIndex, net_base::IPFamily::kIPv6,
                                   0, kTableID))
      .WillOnce(Return(true));
  network_applier_->ApplyRoute(kInterfaceIndex, net_base::IPFamily::kIPv4,
                               gateway, false, true, true, {}, {}, {});
}

TEST_F(NetworkApplierRouteTest, IPv4FixGatewayReachability) {
  const int kInterfaceIndex = 3;
  const auto gateway = net_base::IPAddress::CreateFromString("192.168.1.1");
  const auto gateway_cidr =
      net_base::IPCIDR::CreateFromAddressAndPrefix(*gateway, 32);

  EXPECT_CALL(*routing_table_,
              FlushRoutesWithTag(kInterfaceIndex, net_base::IPFamily::kIPv4));
  EXPECT_CALL(*routing_table_,
              AddRoute(kInterfaceIndex, IsLinkRouteTo(*gateway_cidr)));

  network_applier_->ApplyRoute(kInterfaceIndex, net_base::IPFamily::kIPv4,
                               gateway, true, false, false, {}, {}, {});
}

TEST_F(NetworkApplierRouteTest, IPv4WithStaticRoutes) {
  const int kInterfaceIndex = 3;
  const int kTableID = 1003;
  const auto gateway = net_base::IPAddress::CreateFromString("192.168.1.1");
  const auto excluded_dst =
      net_base::IPCIDR::CreateFromCIDRString("10.1.0.0/16");
  const auto included_dst =
      net_base::IPCIDR::CreateFromCIDRString("10.1.1.0/24");
  const auto rfc3442_dst =
      net_base::IPv4CIDR::CreateFromCIDRString("192.168.200.0/24");
  const auto rfc3442_gateway =
      net_base::IPv4Address::CreateFromString("192.168.100.1");

  EXPECT_CALL(*routing_table_,
              FlushRoutesWithTag(kInterfaceIndex, net_base::IPFamily::kIPv4));
  EXPECT_CALL(*routing_table_,
              SetDefaultRoute(kInterfaceIndex, *gateway, kTableID))
      .WillOnce(Return(true));
  EXPECT_CALL(*routing_table_,
              AddRoute(kInterfaceIndex, IsValidThrowRoute(*excluded_dst)))
      .WillOnce(Return(true));
  EXPECT_CALL(
      *routing_table_,
      AddRoute(kInterfaceIndex, IsValidRouteThrough(*included_dst, *gateway)))
      .WillOnce(Return(true));
  EXPECT_CALL(
      *routing_table_,
      AddRoute(kInterfaceIndex,
               IsValidRouteThrough(net_base::IPCIDR(*rfc3442_dst),
                                   net_base::IPAddress(*rfc3442_gateway))))
      .WillOnce(Return(true));

  network_applier_->ApplyRoute(
      kInterfaceIndex, net_base::IPFamily::kIPv4, gateway, false, true, false,
      {*excluded_dst}, {*included_dst}, {{*rfc3442_dst, *rfc3442_gateway}});
}

TEST_F(NetworkApplierRouteTest, IPv4NoGateway) {
  const int kInterfaceIndex = 3;
  const int kTableID = 1003;
  const auto excluded_dst =
      net_base::IPCIDR::CreateFromCIDRString("10.1.0.0/16");
  const auto included_dst =
      net_base::IPCIDR::CreateFromCIDRString("10.1.1.0/24");
  const auto rfc3442_dst =
      net_base::IPv4CIDR::CreateFromCIDRString("192.168.200.0/24");
  const auto rfc3442_gateway =
      net_base::IPv4Address::CreateFromString("192.168.100.1");

  EXPECT_CALL(*routing_table_,
              FlushRoutesWithTag(kInterfaceIndex, net_base::IPFamily::kIPv4));
  EXPECT_CALL(
      *routing_table_,
      SetDefaultRoute(kInterfaceIndex,
                      net_base::IPAddress(net_base::IPFamily::kIPv4), kTableID))
      .WillOnce(Return(true));
  EXPECT_CALL(*routing_table_,
              AddRoute(kInterfaceIndex, IsValidThrowRoute(*excluded_dst)))
      .WillOnce(Return(true));
  EXPECT_CALL(*routing_table_,
              AddRoute(kInterfaceIndex, IsValidRoute(*included_dst)))
      .WillOnce(Return(true));
  EXPECT_CALL(
      *routing_table_,
      AddRoute(kInterfaceIndex,
               IsValidRouteThrough(net_base::IPCIDR(*rfc3442_dst),
                                   net_base::IPAddress(*rfc3442_gateway))))
      .WillOnce(Return(true));

  network_applier_->ApplyRoute(kInterfaceIndex, net_base::IPFamily::kIPv4,
                               std::nullopt, false, true, false,
                               {*excluded_dst}, {*included_dst},
                               {{*rfc3442_dst, *rfc3442_gateway}});
}

TEST_F(NetworkApplierRouteTest, IPv6) {
  const int kInterfaceIndex = 3;
  const int kTableID = 1003;
  const auto gateway = net_base::IPAddress::CreateFromString("fe80::abcd");
  const auto excluded_dst =
      net_base::IPCIDR::CreateFromCIDRString("2001:db8::/60");
  const auto included_dst =
      net_base::IPCIDR::CreateFromCIDRString("2001:db8:0:1::/64");

  EXPECT_CALL(*routing_table_,
              FlushRoutesWithTag(kInterfaceIndex, net_base::IPFamily::kIPv6));
  EXPECT_CALL(*routing_table_,
              SetDefaultRoute(kInterfaceIndex, *gateway, kTableID))
      .WillOnce(Return(true));
  EXPECT_CALL(*routing_table_,
              AddRoute(kInterfaceIndex, IsValidThrowRoute(*excluded_dst)))
      .WillOnce(Return(true));
  EXPECT_CALL(
      *routing_table_,
      AddRoute(kInterfaceIndex, IsValidRouteThrough(*included_dst, *gateway)))
      .WillOnce(Return(true));

  network_applier_->ApplyRoute(kInterfaceIndex, net_base::IPFamily::kIPv6,
                               gateway, false, true, false, {*excluded_dst},
                               {*included_dst}, {});
}

using NetworkApplierAddressTest = NetworkApplierTest;

TEST_F(NetworkApplierAddressTest, AddAddressFlow) {
  const int kInterfaceIndex = 3;
  const auto ipv4_addr_1 =
      *net_base::IPCIDR::CreateFromCIDRString("192.168.1.2/24");
  const auto ipv4_addr_2 =
      *net_base::IPCIDR::CreateFromCIDRString("192.168.2.2/24");
  const auto ipv6_addr_1 =
      *net_base::IPCIDR::CreateFromCIDRString("2001:db8:0:100::abcd/64");

  EXPECT_CALL(
      address_rtnl_handler_,
      AddInterfaceAddress(kInterfaceIndex, ipv4_addr_1, Eq(std::nullopt)))
      .WillOnce(Return(true));
  network_applier_->ApplyAddress(kInterfaceIndex, ipv4_addr_1, std::nullopt);

  // Adding a second IPv4 address should remove the first one.
  EXPECT_CALL(address_rtnl_handler_,
              RemoveInterfaceAddress(kInterfaceIndex, ipv4_addr_1));
  EXPECT_CALL(
      address_rtnl_handler_,
      AddInterfaceAddress(kInterfaceIndex, ipv4_addr_2, Eq(std::nullopt)))
      .WillOnce(Return(true));
  network_applier_->ApplyAddress(kInterfaceIndex, ipv4_addr_2, std::nullopt);

  // Adding an IPv6 address will not remove the IPv4 one.
  EXPECT_CALL(
      address_rtnl_handler_,
      AddInterfaceAddress(kInterfaceIndex, ipv6_addr_1, Eq(std::nullopt)))
      .WillOnce(Return(true));
  network_applier_->ApplyAddress(kInterfaceIndex, ipv6_addr_1, std::nullopt);

  // Similarly adding an IPv4 address will not remove the IPv6 one.
  EXPECT_CALL(address_rtnl_handler_,
              RemoveInterfaceAddress(kInterfaceIndex, ipv4_addr_2));
  EXPECT_CALL(
      address_rtnl_handler_,
      AddInterfaceAddress(kInterfaceIndex, ipv4_addr_1, Eq(std::nullopt)))
      .WillOnce(Return(true));
  network_applier_->ApplyAddress(kInterfaceIndex, ipv4_addr_1, std::nullopt);
}

TEST_F(NetworkApplierAddressTest, IPv4WithBroadcast) {
  const int kInterfaceIndex = 3;
  const auto local = *net_base::IPCIDR::CreateFromCIDRString("192.168.1.2/24");
  const auto broadcast =
      net_base::IPv4Address::CreateFromString("192.168.1.200");

  EXPECT_CALL(address_rtnl_handler_,
              AddInterfaceAddress(kInterfaceIndex, local, broadcast))
      .WillOnce(Return(true));
  network_applier_->ApplyAddress(kInterfaceIndex, local, broadcast);
}

}  // namespace shill
