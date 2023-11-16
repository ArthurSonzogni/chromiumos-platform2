// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "routing-simulator/route_manager.h"

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <base/logging.h>
#include <base/strings/string_split.h>
#include <net-base/ip_address.h>

#include "routing-simulator/packet.h"
#include "routing-simulator/process_executor.h"
#include "routing-simulator/routing_decision_result.h"
#include "routing-simulator/routing_policy_entry.h"
#include "routing-simulator/routing_table.h"

namespace routing_simulator {
namespace {

bool CheckPriorityOrder(
    const std::vector<RoutingPolicyEntry>& routing_policy_table) {
  if (routing_policy_table.empty()) {
    return false;
  }
  int previous_priority = routing_policy_table.front().priority();
  for (const auto& policy : routing_policy_table) {
    if (previous_priority > policy.priority()) {
      return false;
    }
    previous_priority = policy.priority();
  }
  return true;
}

// Creates a vector that represents a routing policy table from 'ip rule' and
// returns it.
std::vector<RoutingPolicyEntry> BuildRoutingPolicyTable(
    net_base::IPFamily ip_family, std::string_view output) {
  std::vector<RoutingPolicyEntry> routing_policy_table;
  const auto output_lines = base::SplitStringPiece(
      output, "\n\t", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  for (const auto& line : output_lines) {
    const auto policy =
        RoutingPolicyEntry::CreateFromPolicyString(line, ip_family);
    if (!policy) {
      LOG(FATAL) << "Output of 'ip rule' is not valid";
    }
    routing_policy_table.push_back(*policy);
  }
  if (!CheckPriorityOrder(routing_policy_table)) {
    LOG(FATAL) << "Output of 'ip rule' is not sorted by priority";
  }
  return routing_policy_table;
}

// Creates a map that represents routing tables from |output| and returns it.
std::map<std::string, RoutingTable> BuildRoutingTable(
    net_base::IPFamily ip_family, std::string_view output) {
  std::map<std::string, RoutingTable> routing_tables;
  const auto output_lines = base::SplitStringPiece(
      output, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  for (const auto& line : output_lines) {
    const auto route = Route::CreateFromRouteString(line, ip_family);
    if (!route) {
      LOG(FATAL) << "Output of 'ip route' is not valid";
    }
    const auto table_id = route->table_id();
    routing_tables[table_id].AddRoute(*route);
  }
  return routing_tables;
}

}  // namespace

RouteManager::RouteManager(ProcessExecutor* process_executor)
    : process_executor_(process_executor) {}

std::string RouteManager::ExecuteIPRule(net_base::IPFamily ip_family) {
  std::string family = (ip_family == net_base::IPFamily::kIPv4) ? "-4" : "-6";
  auto result = process_executor_->RunAndGetStdout(base::FilePath("/bin/ip"),
                                                   {family, "rule", "show"});
  if (!result) {
    LOG(FATAL) << "output from 'ip rule show' is invalid";
  }
  return *result;
}

std::string RouteManager::ExecuteIPRoute(net_base::IPFamily ip_family) {
  std::string family = (ip_family == net_base::IPFamily::kIPv4) ? "-4" : "-6";
  auto result = process_executor_->RunAndGetStdout(
      base::FilePath("/bin/ip"), {family, "route", "show", "table", "all"});
  if (!result) {
    LOG(FATAL) << "output from 'ip route show' is invalid";
  }
  return *result;
}

RoutingDecisionResult RouteManager::LookUpRoute(const Packet& packet) const {
  const std::map<std::string, RoutingTable>* routing_tables_ptr;
  const std::vector<RoutingPolicyEntry>* routing_policy_table_ptr;
  RoutingDecisionResult result;
  switch (packet.ip_family()) {
    case net_base::IPFamily::kIPv4:
      routing_tables_ptr = &routing_tables_ipv4_;
      routing_policy_table_ptr = &routing_policy_table_ipv4_;
      break;
    case net_base::IPFamily::kIPv6:
      routing_tables_ptr = &routing_tables_ipv6_;
      routing_policy_table_ptr = &routing_policy_table_ipv6_;
      break;
  }
  for (const auto& policy : *routing_policy_table_ptr) {
    if (!policy.Matches(packet)) {
      continue;
    }
    // TODO(b/307460180): Handle the case that the matched route type is throw.
    // Look up a matched route if the routing table that a policy points to
    // exists.
    const Route* matched_route_ptr = nullptr;
    if (const auto it = routing_tables_ptr->find(policy.table_id());
        it != routing_tables_ptr->end()) {
      matched_route_ptr = it->second.LookUpRoute(packet.destination_ip());
    }
    result.AddResult(&policy, matched_route_ptr);
    if (matched_route_ptr) {
      return result;
    }
  }
  return result;
}

void RouteManager::BuildTables() {
  const auto policy_output_ipv4 = ExecuteIPRule(net_base::IPFamily::kIPv4);
  const auto policy_output_ipv6 = ExecuteIPRule(net_base::IPFamily::kIPv6);
  const auto route_output_ipv4 = ExecuteIPRoute(net_base::IPFamily::kIPv4);
  const auto route_output_ipv6 = ExecuteIPRoute(net_base::IPFamily::kIPv6);
  routing_policy_table_ipv4_ =
      BuildRoutingPolicyTable(net_base::IPFamily::kIPv4, policy_output_ipv4);
  routing_policy_table_ipv6_ =
      BuildRoutingPolicyTable(net_base::IPFamily::kIPv6, policy_output_ipv6);
  routing_tables_ipv4_ =
      BuildRoutingTable(net_base::IPFamily::kIPv4, route_output_ipv4);
  routing_tables_ipv6_ =
      BuildRoutingTable(net_base::IPFamily::kIPv6, route_output_ipv6);
}

RoutingDecisionResult RouteManager::ProcessPacketWithMutation(
    Packet& packet) const {
  const auto routing_decision_result = LookUpRoute(packet);
  // If a matched route is found, the last element (pair of matched policy and
  // route) of the vector |routing_decision_result| should have a valid value
  // for second item.
  if (routing_decision_result.result().empty()) {
    return routing_decision_result;
  }
  const auto route = routing_decision_result.result().back().second;
  if (route != nullptr) {
    packet.set_output_interface(route->output_interface());
  }
  return routing_decision_result;
}

}  // namespace routing_simulator
