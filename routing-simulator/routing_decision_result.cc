// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "routing-simulator/routing_decision_result.h"

#include <string>

#include <base/logging.h>

namespace routing_simulator {

RoutingDecisionResult::RoutingDecisionResult() = default;

RoutingDecisionResult::RoutingDecisionResult(
    const RoutingDecisionResult& other) = default;
RoutingDecisionResult& RoutingDecisionResult::operator=(
    const RoutingDecisionResult& other) = default;

void RoutingDecisionResult::Output(std::ostream& std_output) const {
  for (const auto& [policy, route] : result_) {
    if (policy == nullptr) {
      LOG(FATAL) << "Invalid empty policy";
    }
    std_output << "[Pair of matched policy and route]" << std::endl;
    std_output << "policy: " << policy->policy_str() << std::endl;
    const auto route_str = route ? route->route_str() : "no route matched";
    std_output << route_str << std::endl;
  }
}

}  // namespace routing_simulator
