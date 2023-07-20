// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NETWORK_MOCK_ROUTING_POLICY_SERVICE_H_
#define SHILL_NETWORK_MOCK_ROUTING_POLICY_SERVICE_H_

#include <vector>

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
  MOCK_METHOD(const std::vector<uint32_t>&, GetUserTrafficUids, (), (override));
  MOCK_METHOD(uint32_t, GetShillUid, (), (override));
};

}  // namespace shill

#endif  // SHILL_NETWORK_MOCK_ROUTING_POLICY_SERVICE_H_
