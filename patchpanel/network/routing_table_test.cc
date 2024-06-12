// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/network/routing_table.h"

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
#include <net-base/mock_rtnl_handler.h>
#include <net-base/rtnl_message.h>

using testing::_;
using testing::Field;
using testing::Invoke;
using testing::Return;
using testing::StrictMock;
using testing::Test;
using testing::WithArg;

namespace patchpanel {

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
  static const int kTestDeviceIndex0;
  static const int kTestDeviceIndex1;
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

const int RoutingTableTest::kTestDeviceIndex0 = 12345;
const int RoutingTableTest::kTestDeviceIndex1 = 67890;
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

void RoutingTableTest::Start() {
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

}  // namespace patchpanel
