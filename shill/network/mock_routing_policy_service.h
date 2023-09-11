// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NETWORK_MOCK_ROUTING_POLICY_SERVICE_H_
#define SHILL_NETWORK_MOCK_ROUTING_POLICY_SERVICE_H_

#include <string_view>

#include <base/containers/flat_map.h>
#include <gmock/gmock.h>

#include "shill/network/routing_policy_service.h"

namespace shill {

class MockRoutingPolicyService : public RoutingPolicyService {
 public:
  MockRoutingPolicyService();
  MockRoutingPolicyService(const MockRoutingPolicyService&) = delete;
  MockRoutingPolicyService& operator=(const MockRoutingPolicyService&) = delete;

  ~MockRoutingPolicyService() override;

  MOCK_METHOD(void, Start, (), (override));
  MOCK_METHOD(void, Stop, (), (override));
  MOCK_METHOD(bool, AddRule, (int, const RoutingPolicyEntry&), (override));
  MOCK_METHOD(void, FlushRules, (int), (override));
  MOCK_METHOD((const base::flat_map<std::string_view, fib_rule_uid_range>&),
              GetUserTrafficUids,
              (),
              (override));
  MOCK_METHOD(fib_rule_uid_range, GetChromeUid, (), (override));
};

}  // namespace shill

#endif  // SHILL_NETWORK_MOCK_ROUTING_POLICY_SERVICE_H_
