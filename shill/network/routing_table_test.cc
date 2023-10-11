// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/routing_table.h"

#include <linux/rtnetlink.h>
#include <sys/socket.h>

#include <memory>
#include <vector>

#include <base/check.h>
#include <base/containers/contains.h>
#include <base/functional/bind.h>
#include <base/functional/callback.h>
#include <base/functional/callback_helpers.h>
#include <base/memory/weak_ptr.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <net-base/byte_utils.h>
#include <net-base/ip_address.h>
#include <net-base/rtnl_message.h>
#include <net-base/mock_rtnl_handler.h>

using testing::_;
using testing::Field;
using testing::Invoke;
using testing::Return;
using testing::StrictMock;
using testing::Test;
using testing::WithArg;

namespace shill {

class RoutingTableTest : public Test {
 public:
  RoutingTableTest() : routing_table_(new RoutingTable()) {}

  void SetUp() override {
    routing_table_->rtnl_handler_ = &rtnl_handler_;
    ON_CALL(rtnl_handler_, DoSendMessage(_, _)).WillByDefault(Return(true));
  }

  std::unordered_map<int, std::vector<RoutingTableEntry>>* GetRoutingTables() {
    return &routing_table_->tables_;
  }

  void SendRouteEntry(net_base::RTNLMessage::Mode mode,
                      uint32_t interface_index,
                      const RoutingTableEntry& entry);

  void SendRouteEntryWithSeqAndProto(net_base::RTNLMessage::Mode mode,
                                     uint32_t interface_index,
                                     const RoutingTableEntry& entry,
                                     uint32_t seq,
                                     unsigned char proto);

  void Start();

  bool SetSequenceForMessage(uint32_t* seq) {
    *seq = RoutingTableTest::kTestRequestSeq;
    return true;
  }

 protected:
  static const uint32_t kTestDeviceIndex0;
  static const uint32_t kTestDeviceIndex1;
  static const char kTestDeviceName0[];
  static const char kTestDeviceNetAddress4[];
  static const char kTestForeignNetAddress4[];
  static const char kTestForeignNetGateway4[];
  static const char kTestForeignNetAddress6[];
  static const char kTestForeignNetGateway6[];
  static const char kTestGatewayAddress4[];
  static const char kTestNetAddress0[];
  static const char kTestNetAddress1[];
  static const char kTestV6NetAddress0[];
  static const char kTestV6NetAddress1[];
  static const char kTestRemoteAddress4[];
  static const char kTestRemoteNetwork4[];
  static const int kTestRemotePrefix4;
  static const uint32_t kTestRequestSeq;
  static const int kTestRouteTag;

  class QueryCallbackTarget {
   public:
    QueryCallbackTarget() : weak_ptr_factory_(this) {}

    MOCK_METHOD(void, MockedTarget, (int, const RoutingTableEntry&));

    void UnreachedTarget(int interface_index, const RoutingTableEntry& entry) {
      CHECK(false);
    }

   private:
    base::WeakPtrFactory<QueryCallbackTarget> weak_ptr_factory_;
  };

  std::unique_ptr<RoutingTable> routing_table_;
  StrictMock<net_base::MockRTNLHandler> rtnl_handler_;
};

const uint32_t RoutingTableTest::kTestDeviceIndex0 = 12345;
const uint32_t RoutingTableTest::kTestDeviceIndex1 = 67890;
const char RoutingTableTest::kTestDeviceName0[] = "test-device0";
const char RoutingTableTest::kTestDeviceNetAddress4[] = "192.168.2.0/24";
const char RoutingTableTest::kTestForeignNetAddress4[] = "192.168.2.2";
const char RoutingTableTest::kTestForeignNetGateway4[] = "192.168.2.1";
const char RoutingTableTest::kTestForeignNetAddress6[] = "2000::/3";
const char RoutingTableTest::kTestForeignNetGateway6[] = "fe80:::::1";
const char RoutingTableTest::kTestGatewayAddress4[] = "192.168.2.254";
const char RoutingTableTest::kTestNetAddress0[] = "192.168.1.1";
const char RoutingTableTest::kTestNetAddress1[] = "192.168.1.2";
const char RoutingTableTest::kTestV6NetAddress0[] = "2001:db8::123";
const char RoutingTableTest::kTestV6NetAddress1[] = "2001:db8::456";
const char RoutingTableTest::kTestRemoteAddress4[] = "192.168.2.254";
const char RoutingTableTest::kTestRemoteNetwork4[] = "192.168.100.0";
const int RoutingTableTest::kTestRemotePrefix4 = 24;
const uint32_t RoutingTableTest::kTestRequestSeq = 456;
const int RoutingTableTest::kTestRouteTag = 789;

namespace {

MATCHER_P3(IsBlackholeRoutingPacket, family, metric, table, "") {
  const net_base::RTNLMessage::RouteStatus& status = arg->route_status();

  const auto priority = net_base::byte_utils::FromBytes<uint32_t>(
      arg->GetAttribute(RTA_PRIORITY));

  return arg->type() == net_base::RTNLMessage::kTypeRoute &&
         arg->family() == net_base::ToSAFamily(family) &&
         arg->flags() == (NLM_F_REQUEST | NLM_F_CREATE | NLM_F_EXCL) &&
         status.table == table && status.protocol == RTPROT_BOOT &&
         status.scope == RT_SCOPE_UNIVERSE && status.type == RTN_BLACKHOLE &&
         !arg->HasAttribute(RTA_DST) && !arg->HasAttribute(RTA_SRC) &&
         !arg->HasAttribute(RTA_GATEWAY) && priority && *priority == metric;
}

MATCHER_P2(IsUnreachableRoutingPacket, family, table, "") {
  const net_base::RTNLMessage::RouteStatus& status = arg->route_status();

  return arg->type() == net_base::RTNLMessage::kTypeRoute &&
         arg->family() == net_base::ToSAFamily(family) &&
         arg->flags() == (NLM_F_REQUEST | NLM_F_CREATE | NLM_F_EXCL) &&
         status.table == table && status.protocol == RTPROT_BOOT &&
         status.scope == RT_SCOPE_UNIVERSE && status.type == RTN_UNREACHABLE &&
         !arg->HasAttribute(RTA_SRC) && !arg->HasAttribute(RTA_GATEWAY);
}

MATCHER_P4(IsRoutingPacket, mode, index, entry, flags, "") {
  const net_base::RTNLMessage::RouteStatus& status = arg->route_status();

  const auto oif =
      net_base::byte_utils::FromBytes<uint32_t>(arg->GetAttribute(RTA_OIF));
  const auto priority = net_base::byte_utils::FromBytes<uint32_t>(
      arg->GetAttribute(RTA_PRIORITY));

  return arg->type() == net_base::RTNLMessage::kTypeRoute &&
         arg->family() == net_base::ToSAFamily(entry.gateway.GetFamily()) &&
         arg->flags() == (NLM_F_REQUEST | flags) &&
         entry.table == RoutingTable::GetInterfaceTableId(index) &&
         status.protocol == RTPROT_BOOT && status.scope == entry.scope &&
         status.type == RTN_UNICAST && arg->HasAttribute(RTA_DST) &&
         arg->GetRtaDst() == entry.dst &&
         ((!arg->HasAttribute(RTA_SRC) && entry.src.IsDefault()) ||
          arg->GetRtaSrc() == entry.src) &&
         ((!arg->HasAttribute(RTA_GATEWAY) && entry.gateway.IsZero()) ||
          arg->GetRtaGateway() == entry.gateway) &&
         oif && *oif == index && priority && *priority == entry.metric;
}

}  // namespace

void RoutingTableTest::SendRouteEntry(net_base::RTNLMessage::Mode mode,
                                      uint32_t interface_index,
                                      const RoutingTableEntry& entry) {
  SendRouteEntryWithSeqAndProto(mode, interface_index, entry, 0, RTPROT_BOOT);
}

void RoutingTableTest::SendRouteEntryWithSeqAndProto(
    net_base::RTNLMessage::Mode mode,
    uint32_t interface_index,
    const RoutingTableEntry& entry,
    uint32_t seq,
    unsigned char proto) {
  net_base::RTNLMessage msg(net_base::RTNLMessage::kTypeRoute, mode, 0, seq, 0,
                            0, net_base::ToSAFamily(entry.dst.GetFamily()));

  msg.set_route_status(net_base::RTNLMessage::RouteStatus(
      entry.dst.prefix_length(), entry.src.prefix_length(),
      entry.table < 256 ? entry.table : RT_TABLE_COMPAT, proto, entry.scope,
      RTN_UNICAST, 0));

  msg.SetAttribute(RTA_DST, entry.dst.address().ToBytes());
  if (!entry.src.address().IsZero()) {
    msg.SetAttribute(RTA_SRC, entry.src.address().ToBytes());
  }
  if (!entry.gateway.IsZero()) {
    msg.SetAttribute(RTA_GATEWAY, entry.gateway.ToBytes());
  }
  msg.SetAttribute(RTA_TABLE,
                   net_base::byte_utils::ToBytes<uint32_t>(entry.table));
  msg.SetAttribute(RTA_PRIORITY,
                   net_base::byte_utils::ToBytes<uint32_t>(entry.metric));
  msg.SetAttribute(RTA_OIF,
                   net_base::byte_utils::ToBytes<uint32_t>(interface_index));

  routing_table_->RouteMsgHandler(msg);
}

void RoutingTableTest::Start() {
  EXPECT_CALL(rtnl_handler_, RequestDump(net_base::RTNLHandler::kRequestRoute));
  EXPECT_CALL(rtnl_handler_,
              DoSendMessage(
                  IsUnreachableRoutingPacket(net_base::IPFamily::kIPv6,
                                             RoutingTable::kUnreachableTableId),
                  _));
  EXPECT_CALL(rtnl_handler_,
              DoSendMessage(
                  IsUnreachableRoutingPacket(net_base::IPFamily::kIPv4,
                                             RoutingTable::kUnreachableTableId),
                  _));
  routing_table_->Start();
}

TEST_F(RoutingTableTest, Start) {
  Start();
}

TEST_F(RoutingTableTest, RouteAddDelete) {
  // Expect the tables to be empty by default.
  EXPECT_EQ(0, GetRoutingTables()->size());

  const auto default_address = net_base::IPCIDR(net_base::IPFamily::kIPv4);
  const auto gateway_address0 =
      *net_base::IPAddress::CreateFromString(kTestNetAddress0);

  int metric = 10;

  // Add a single entry.
  auto entry0 =
      RoutingTableEntry(default_address, default_address, gateway_address0)
          .SetMetric(metric)
          .SetTable(RoutingTable::GetInterfaceTableId(kTestDeviceIndex0));
  SendRouteEntry(net_base::RTNLMessage::kModeAdd, kTestDeviceIndex0, entry0);

  std::unordered_map<int, std::vector<RoutingTableEntry>>* tables =
      GetRoutingTables();

  // We should have a single table, which should in turn have a single entry.
  EXPECT_EQ(1, tables->size());
  EXPECT_TRUE(base::Contains(*tables, kTestDeviceIndex0));
  EXPECT_EQ(1, (*tables)[kTestDeviceIndex0].size());

  RoutingTableEntry test_entry = (*tables)[kTestDeviceIndex0][0];
  EXPECT_EQ(entry0, test_entry);

  // Add a second entry for a different interface.
  auto entry1 =
      RoutingTableEntry(default_address, default_address, gateway_address0)
          .SetMetric(metric)
          .SetTable(RoutingTable::GetInterfaceTableId(kTestDeviceIndex1));
  SendRouteEntry(net_base::RTNLMessage::kModeAdd, kTestDeviceIndex1, entry1);

  // We should have two tables, which should have a single entry each.
  EXPECT_EQ(2, tables->size());
  EXPECT_TRUE(base::Contains(*tables, kTestDeviceIndex1));
  EXPECT_EQ(1, (*tables)[kTestDeviceIndex0].size());
  EXPECT_EQ(1, (*tables)[kTestDeviceIndex1].size());

  test_entry = (*tables)[kTestDeviceIndex1][0];
  EXPECT_EQ(entry1, test_entry);

  const auto gateway_address1 =
      *net_base::IPAddress::CreateFromString(kTestNetAddress1);

  auto entry2 =
      RoutingTableEntry(default_address, default_address, gateway_address1);

  // Add a second gateway route to the second interface.
  SendRouteEntry(net_base::RTNLMessage::kModeAdd, kTestDeviceIndex1, entry2);

  // We should have two tables, one of which has a single entry, the other has
  // two.
  EXPECT_EQ(2, tables->size());
  EXPECT_EQ(1, (*tables)[kTestDeviceIndex0].size());
  EXPECT_EQ(2, (*tables)[kTestDeviceIndex1].size());

  test_entry = (*tables)[kTestDeviceIndex1][1];
  EXPECT_EQ(entry2, test_entry);

  // Remove the first gateway route from the second interface.
  SendRouteEntry(net_base::RTNLMessage::kModeDelete, kTestDeviceIndex1, entry1);

  // We should be back to having one route per table.
  EXPECT_EQ(2, tables->size());
  EXPECT_EQ(1, (*tables)[kTestDeviceIndex0].size());
  EXPECT_EQ(1, (*tables)[kTestDeviceIndex1].size());

  test_entry = (*tables)[kTestDeviceIndex1][0];
  EXPECT_EQ(entry2, test_entry);

  // Send a duplicate of the second gateway route message, changing the metric.
  RoutingTableEntry entry3(entry2);
  entry3.metric++;
  SendRouteEntry(net_base::RTNLMessage::kModeAdd, kTestDeviceIndex1, entry3);

  // Both entries should show up.
  EXPECT_EQ(2, (*tables)[kTestDeviceIndex1].size());
  test_entry = (*tables)[kTestDeviceIndex1][0];
  EXPECT_EQ(entry2, test_entry);
  test_entry = (*tables)[kTestDeviceIndex1][1];
  EXPECT_EQ(entry3, test_entry);

  // Find a matching entry.
  EXPECT_TRUE(routing_table_->GetDefaultRoute(
      kTestDeviceIndex1, net_base::IPFamily::kIPv4, &test_entry));
  EXPECT_EQ(entry2, test_entry);

  // Test that a search for a non-matching family fails.
  EXPECT_FALSE(routing_table_->GetDefaultRoute(
      kTestDeviceIndex1, net_base::IPFamily::kIPv6, &test_entry));

  // Remove last entry from an existing interface and test that we now fail.
  SendRouteEntry(net_base::RTNLMessage::kModeDelete, kTestDeviceIndex1, entry2);
  SendRouteEntry(net_base::RTNLMessage::kModeDelete, kTestDeviceIndex1, entry3);

  EXPECT_FALSE(routing_table_->GetDefaultRoute(
      kTestDeviceIndex1, net_base::IPFamily::kIPv4, &test_entry));

  // Add a route to a gateway address.
  const auto gateway_address =
      *net_base::IPAddress::CreateFromString(kTestNetAddress0);

  RoutingTableEntry entry4(entry1);
  entry4.SetMetric(RoutingTable::kShillDefaultRouteMetric);
  EXPECT_CALL(rtnl_handler_,
              DoSendMessage(IsRoutingPacket(net_base::RTNLMessage::kModeAdd,
                                            kTestDeviceIndex1, entry4,
                                            NLM_F_CREATE | NLM_F_EXCL),
                            _));
  EXPECT_TRUE(routing_table_->SetDefaultRoute(
      kTestDeviceIndex1, gateway_address,
      RoutingTable::GetInterfaceTableId(kTestDeviceIndex1)));

  // Test that removing the table causes the route to disappear.
  routing_table_->ResetTable(kTestDeviceIndex1);
  EXPECT_FALSE(base::Contains(*tables, kTestDeviceIndex1));
  EXPECT_FALSE(routing_table_->GetDefaultRoute(
      kTestDeviceIndex1, net_base::IPFamily::kIPv4, &test_entry));
  EXPECT_EQ(1, GetRoutingTables()->size());

  // Ask to flush table0.  We should see a delete message sent.
  EXPECT_CALL(rtnl_handler_,
              DoSendMessage(IsRoutingPacket(net_base::RTNLMessage::kModeDelete,
                                            kTestDeviceIndex0, entry0, 0),
                            _));
  routing_table_->FlushRoutes(kTestDeviceIndex0);
  EXPECT_EQ(0, (*tables)[kTestDeviceIndex0].size());

  // Test that the routing table size returns to zero.
  SendRouteEntry(net_base::RTNLMessage::kModeAdd, kTestDeviceIndex0, entry1);
  EXPECT_EQ(1, GetRoutingTables()->size());
  routing_table_->ResetTable(kTestDeviceIndex0);
  EXPECT_EQ(0, GetRoutingTables()->size());
}

TEST_F(RoutingTableTest, LowestMetricDefault) {
  // Expect the tables to be empty by default.
  EXPECT_EQ(0, GetRoutingTables()->size());

  const auto default_address = net_base::IPCIDR(net_base::IPFamily::kIPv4);
  const auto gateway_address0 =
      *net_base::IPAddress::CreateFromString(kTestNetAddress0);

  auto entry =
      RoutingTableEntry(default_address, default_address, gateway_address0)
          .SetMetric(2)
          .SetTable(RoutingTable::GetInterfaceTableId(kTestDeviceIndex0));

  // Add the same entry three times, with different metrics.
  SendRouteEntry(net_base::RTNLMessage::kModeAdd, kTestDeviceIndex0, entry);

  entry.metric = 1;
  SendRouteEntry(net_base::RTNLMessage::kModeAdd, kTestDeviceIndex0, entry);

  entry.metric = 1024;
  SendRouteEntry(net_base::RTNLMessage::kModeAdd, kTestDeviceIndex0, entry);

  // Find a matching entry.
  RoutingTableEntry test_entry(net_base::IPFamily::kIPv4);
  EXPECT_TRUE(routing_table_->GetDefaultRoute(
      kTestDeviceIndex0, net_base::IPFamily::kIPv4, &test_entry));
  entry.metric = 1;
  EXPECT_EQ(entry, test_entry);
}

TEST_F(RoutingTableTest, IPv6StatelessAutoconfiguration) {
  // Expect the tables to be empty by default.
  EXPECT_EQ(0, GetRoutingTables()->size());

  const auto default_address = net_base::IPCIDR(net_base::IPFamily::kIPv6);
  const auto gateway_address =
      *net_base::IPAddress::CreateFromString(kTestV6NetAddress0);

  auto entry0 =
      RoutingTableEntry(default_address, default_address, gateway_address)
          .SetMetric(1024)
          .SetTable(RoutingTable::GetInterfaceTableId(kTestDeviceIndex0));
  entry0.protocol = RTPROT_RA;

  // Simulate an RTPROT_RA kernel message indicating that it processed a
  // valid IPv6 router advertisement.
  SendRouteEntryWithSeqAndProto(net_base::RTNLMessage::kModeAdd,
                                kTestDeviceIndex0, entry0, 0 /* seq */,
                                RTPROT_RA);

  std::unordered_map<int, std::vector<RoutingTableEntry>>* tables =
      GetRoutingTables();

  // We should have a single table, which should in turn have a single entry.
  EXPECT_EQ(1, tables->size());
  EXPECT_TRUE(base::Contains(*tables, kTestDeviceIndex0));
  EXPECT_EQ(1, (*tables)[kTestDeviceIndex0].size());

  RoutingTableEntry test_entry = (*tables)[kTestDeviceIndex0][0];
  EXPECT_EQ(entry0, test_entry);

  // Now send an RTPROT_RA netlink message advertising some other random
  // host.  shill should ignore these because they are frequent, and
  // not worth tracking.

  const auto non_default_address =
      *net_base::IPCIDR::CreateFromStringAndPrefix(kTestV6NetAddress1, 128);

  auto entry2 =
      RoutingTableEntry(non_default_address, default_address, gateway_address)
          .SetMetric(1024)
          .SetTable(RoutingTable::GetInterfaceTableId(kTestDeviceIndex0));

  // Simulate an RTPROT_RA kernel message.
  SendRouteEntryWithSeqAndProto(net_base::RTNLMessage::kModeAdd,
                                kTestDeviceIndex0, entry2, 0 /* seq */,
                                RTPROT_RA);

  tables = GetRoutingTables();
  EXPECT_EQ(1, tables->size());
}

MATCHER_P2(IsRoutingQuery, destination, index, "") {
  const net_base::RTNLMessage::RouteStatus& status = arg->route_status();
  const auto oif =
      net_base::byte_utils::FromBytes<uint32_t>(arg->GetAttribute(RTA_OIF));

  return arg->type() == net_base::RTNLMessage::kTypeRoute &&
         arg->family() == destination.family() &&
         arg->flags() == NLM_F_REQUEST && status.table == 0 &&
         status.protocol == 0 && status.scope == 0 && status.type == 0 &&
         arg->HasAttribute(RTA_DST) &&
         arg->GetRtaDst() == destination.ToIPCIDR() &&
         !arg->HasAttribute(RTA_SRC) && !arg->HasAttribute(RTA_GATEWAY) &&
         oif && oif == index && !arg->HasAttribute(RTA_PRIORITY);

  return false;
}

TEST_F(RoutingTableTest, CreateBlackholeRoute) {
  const uint32_t kMetric = 2;
  const uint32_t kTestTable = 20;
  EXPECT_CALL(rtnl_handler_,
              DoSendMessage(IsBlackholeRoutingPacket(net_base::IPFamily::kIPv6,
                                                     kMetric, kTestTable),
                            _))
      .Times(1);
  EXPECT_TRUE(routing_table_->CreateBlackholeRoute(
      kTestDeviceIndex0, net_base::IPFamily::kIPv6, kMetric, kTestTable));
}

}  // namespace shill
