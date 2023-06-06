// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_ROUTING_POLICY_SERVICE_H_
#define SHILL_ROUTING_POLICY_SERVICE_H_

#include <memory>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

#include <base/no_destructor.h>

#include "shill/net/ip_address.h"
#include "shill/net/rtnl_message.h"
#include "shill/refptr_types.h"
#include "shill/routing_policy_entry.h"

namespace shill {

class RTNLHandler;
class RTNLListener;

class RoutingPolicyService {
 public:
  // Priority of the rule sending all traffic to the local routing table.
  static constexpr uint32_t kRulePriorityLocal = 0;
  // Priority of the rule sending all traffic to the main routing table.
  static constexpr uint32_t kRulePriorityMain = 32766;

  virtual ~RoutingPolicyService();

  static RoutingPolicyService* GetInstance();

  virtual void Start();
  virtual void Stop();

  // Add an entry to the routing rule table.
  virtual bool AddRule(int interface_index, const RoutingPolicyEntry& entry);

  // Flush all routing rules for |interface_index|.
  virtual void FlushRules(int interface_index);

  // Returns the user traffic uids.
  const std::vector<uint32_t>& GetUserTrafficUids();

 protected:
  RoutingPolicyService();
  RoutingPolicyService(const RoutingPolicyService&) = delete;
  RoutingPolicyService& operator=(const RoutingPolicyService&) = delete;

 private:
  friend class base::NoDestructor<RoutingPolicyService>;
  friend class RoutingPolicyServiceTest;
  using PolicyTableEntryVector = std::vector<RoutingPolicyEntry>;
  using PolicyTables = std::unordered_map<int, PolicyTableEntryVector>;

  void RuleMsgHandler(const RTNLMessage& message);

  bool ApplyRule(uint32_t interface_index,
                 const RoutingPolicyEntry& entry,
                 RTNLMessage::Mode mode,
                 unsigned int flags);
  bool ParseRoutingPolicyMessage(const RTNLMessage& message,
                                 RoutingPolicyEntry* entry);

  PolicyTables policy_tables_;
  std::set<int> managed_interfaces_;

  std::unique_ptr<RTNLListener> rule_listener_;

  // "User traffic" refers to traffic from processes that run under one of the
  // unix users enumered in |kUserTrafficUsernames| constant in
  // shill/routing_table.cc.
  std::vector<uint32_t> user_traffic_uids_;

  // Cache singleton pointer for performance and test purposes.
  RTNLHandler* rtnl_handler_;
};

}  // namespace shill

#endif  // SHILL_ROUTING_POLICY_SERVICE_H_
