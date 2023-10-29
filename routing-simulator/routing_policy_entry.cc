// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "routing-simulator/routing_policy_entry.h"

namespace routing_simulator {

// static
// TODO(b/307460180): Implement below
std::optional<RoutingPolicyEntry>
RoutingPolicyEntry::CreateFromPolicyEntryString(std::string_view policy_string,
                                                net_base::IPFamily ip_family) {
  return std::nullopt;
}

RoutingPolicyEntry::RoutingPolicyEntry(const RoutingPolicyEntry& other) =
    default;
RoutingPolicyEntry& RoutingPolicyEntry::operator=(
    const RoutingPolicyEntry& other) = default;

bool RoutingPolicyEntry::operator==(const RoutingPolicyEntry& rhs) const =
    default;

RoutingPolicyEntry::RoutingPolicyEntry(net_base::IPFamily ip_family)
    : source_prefix_(net_base::IPCIDR(ip_family)) {}

}  // namespace routing_simulator
