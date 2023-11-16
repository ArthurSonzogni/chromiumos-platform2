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
  if (result_.empty()) {
    std_output << "[FAIL] There is no policy matched found" << std::endl;
    return;
  }
  for (const auto& [policy, route] : result_) {
    if (policy == nullptr) {
      LOG(FATAL) << "Invalid empty policy";
    }
    std_output << "policy: " << policy->policy_str() << std::endl;
    const auto route_str = route ? route->route_str() : "no route matched";
    std_output << "route: " << route_str << std::endl;
  }
  const auto matched_route = result_.back().second;
  if (matched_route == nullptr) {
    std_output << "[FAIL] No matched route found for this packet" << std::endl;
    return;
  }
  std_output << "[SUCCESS] Routing of this packet is successful" << std::endl;
  std_output << "[destination prefix] "
             << matched_route->destination_prefix().ToString() << std::endl;
}

void RoutingDecisionResult::AddResult(const RoutingPolicyEntry* policy,
                                      const Route* route_ptr) {
  if (policy == nullptr) {
    LOG(ERROR) << "Invalid empty policy: cannot add empty policy";
    return;
  }
  result_.push_back(std::make_pair(policy, route_ptr));
}

}  // namespace routing_simulator
