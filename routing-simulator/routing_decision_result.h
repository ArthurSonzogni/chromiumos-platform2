// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ROUTING_SIMULATOR_ROUTING_DECISION_RESULT_H_
#define ROUTING_SIMULATOR_ROUTING_DECISION_RESULT_H_

#include "routing-simulator/result.h"

#include <ostream>
#include <utility>
#include <vector>

#include "routing-simulator/route.h"
#include "routing-simulator/routing_policy_entry.h"

namespace routing_simulator {
class RoutingDecisionResult : public Result {
 public:
  // Pair of matched policy and route which the policy points to, which can be
  // nullptr if there is no route matched.
  using Entry = std::pair<const RoutingPolicyEntry*, const Route*>;

  RoutingDecisionResult();

  // RoutingDecisionResult is copyable.
  RoutingDecisionResult(const RoutingDecisionResult& other);
  RoutingDecisionResult& operator=(const RoutingDecisionResult& other);

  // Outputs the result (a list of pairs of matched policy and route).
  void Output(std::ostream& std_output) const override;

 private:
  // A list of pairs of matched policy and matched route.
  // RoutingPolicyEntry* never be nullptr, while Route* can be since there are
  // some cases in which no matched route for a certain matched policy
  // is not found.
  std::vector<Entry> result_;
};

}  // namespace routing_simulator

#endif  // ROUTING_SIMULATOR_ROUTING_DECISION_RESULT_H_
