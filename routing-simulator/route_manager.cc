// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "routing-simulator/route_manager.h"

#include <map>
#include <optional>
#include <string>
#include <string_view>

#include <base/logging.h>
#include <base/strings/string_split.h>
#include <net-base/ip_address.h>

#include "routing-simulator/packet.h"
#include "routing-simulator/process_executor.h"
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

// TODO(b/307460180): Change the interface (output/input parameter or
// returned value type) to save the matched policy with matched routes (or
// no matched route for the policy).
const Route* RouteManager::LookUpRoute(const Packet& packet) const {
  const std::map<std::string, RoutingTable>* routing_tables_ptr;
  const std::vector<RoutingPolicyEntry>* routing_policy_table_ptr;
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
    // Look up a matched route if the routing table that a policy points to
    // exists.
    if (const auto it = routing_tables_ptr->find(policy.table_id());
        it != routing_tables_ptr->end()) {
      const auto* matched_route_ptr =
          it->second.LookUpRoute(packet.destination_ip());
      if (!matched_route_ptr) {
        continue;
      }
      return matched_route_ptr;
    }
  }

  return nullptr;
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

// TODO(b/307460180): Change the return value to contains both matched
// route and the related policy entry.
const Route* RouteManager::ProcessPacketWithMutation(Packet& packet) const {
  const auto matched_route_ptr = LookUpRoute(packet);
  if (matched_route_ptr) {
    packet.set_output_interface(matched_route_ptr->output_interface());
  }
  return matched_route_ptr;
}

}  // namespace routing_simulator
