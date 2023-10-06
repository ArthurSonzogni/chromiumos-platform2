// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/routing_policy_service.h"

#include <linux/fib_rules.h>
#include <linux/rtnetlink.h>
#include <sys/socket.h>

#include <base/memory/ptr_util.h>
#include <net-base/ip_address.h>
#include <net-base/mock_rtnl_handler.h>

using testing::_;
using testing::Field;
using testing::Invoke;
using testing::Return;
using testing::StrictMock;
using testing::Test;
using testing::WithArg;

namespace shill {

class RoutingPolicyServiceTest : public Test {
 public:
  void SetUp() override {
    // Using `new` to access a non-public constructor.
    rule_table_ =
        base::WrapUnique<RoutingPolicyService>(new RoutingPolicyService());
    rule_table_->rtnl_handler_ = &rtnl_handler_;
    ON_CALL(rtnl_handler_, DoSendMessage).WillByDefault(Return(true));
  }

  void Start();

  int CountRoutingPolicyEntries();

 protected:
  std::unique_ptr<RoutingPolicyService> rule_table_;
  StrictMock<net_base::MockRTNLHandler> rtnl_handler_;
};

int RoutingPolicyServiceTest::CountRoutingPolicyEntries() {
  int count = 0;
  for (const auto& table : rule_table_->policy_tables_) {
    for (auto nent = table.second.begin(); nent != table.second.end(); nent++) {
      count++;
    }
  }
  return count;
}

void RoutingPolicyServiceTest::Start() {
  EXPECT_CALL(rtnl_handler_, RequestDump(net_base::RTNLHandler::kRequestRule));
  rule_table_->Start();
}

TEST_F(RoutingPolicyServiceTest, Start) {
  Start();
}

TEST_F(RoutingPolicyServiceTest, PolicyRuleAddFlush) {
  Start();

  // Expect the tables to be empty by default.
  EXPECT_EQ(CountRoutingPolicyEntries(), 0);

  const int iface_id0 = 3;
  const int iface_id1 = 4;

  EXPECT_CALL(rtnl_handler_, DoSendMessage(_, _)).WillOnce(Return(true));
  auto rule = RoutingPolicyEntry(net_base::IPFamily::kIPv4);
  rule.priority = 100;
  rule.table = 1001;
  rule.uid_range = fib_rule_uid_range{1000, 2000};
  EXPECT_TRUE(rule_table_->AddRule(iface_id0, rule));
  EXPECT_EQ(CountRoutingPolicyEntries(), 1);

  EXPECT_CALL(rtnl_handler_, DoSendMessage(_, _)).WillOnce(Return(true));
  rule = RoutingPolicyEntry(net_base::IPFamily::kIPv4);
  rule.priority = 101;
  rule.table = 1002;
  rule.iif_name = "arcbr0";
  EXPECT_TRUE(rule_table_->AddRule(iface_id0, rule));
  EXPECT_EQ(CountRoutingPolicyEntries(), 2);

  EXPECT_CALL(rtnl_handler_, DoSendMessage(_, _)).WillOnce(Return(true));
  rule = RoutingPolicyEntry(net_base::IPFamily::kIPv4);
  rule.priority = 102;
  rule.table = 1003;
  rule.uid_range = fib_rule_uid_range{100, 101};
  EXPECT_TRUE(rule_table_->AddRule(iface_id1, rule));
  EXPECT_EQ(CountRoutingPolicyEntries(), 3);

  EXPECT_CALL(rtnl_handler_, DoSendMessage(_, _))
      .Times(2)
      .WillRepeatedly(Return(true));
  rule_table_->FlushRules(iface_id0);
  EXPECT_EQ(CountRoutingPolicyEntries(), 1);

  EXPECT_CALL(rtnl_handler_, DoSendMessage(_, _)).WillOnce(Return(true));
  rule_table_->FlushRules(iface_id1);
  EXPECT_EQ(CountRoutingPolicyEntries(), 0);
}

}  // namespace shill
