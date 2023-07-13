// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_ROUTING_POLICY_ENTRY_H_
#define SHILL_ROUTING_POLICY_ENTRY_H_

// Add for fib_rule_uid_range definition.
#include <linux/fib_rules.h>

#include <optional>
#include <string>

#include <net-base/ip_address.h>

bool operator==(const fib_rule_uid_range& a, const fib_rule_uid_range& b);

namespace shill {

// Represents a single policy routing rule.
// ctor will initialize |dst| and |src| to be of the same family of |family|,
// but caller needs to ensure the family still matches when they set |dst| or
// |src| afterwards.
struct RoutingPolicyEntry {
  struct FwMark {
    uint32_t value = 0;
    uint32_t mask = 0xFFFFFFFF;

    bool operator==(const FwMark& b) const {
      return (value == b.value) && (mask == b.mask);
    }
  };

  explicit RoutingPolicyEntry(net_base::IPFamily family);

  bool operator==(const RoutingPolicyEntry& b) const;

  net_base::IPFamily family;
  uint32_t priority = 1;
  uint32_t table = RT_TABLE_MAIN;

  net_base::IPCIDR dst;
  net_base::IPCIDR src;

  std::optional<FwMark> fw_mark;
  std::optional<fib_rule_uid_range> uid_range;
  std::optional<std::string> iif_name;
  std::optional<std::string> oif_name;

  bool invert_rule = false;
};

// Print out an entry in a format similar to that of ip rule.
std::ostream& operator<<(std::ostream& os, const RoutingPolicyEntry& entry);

}  // namespace shill

#endif  // SHILL_ROUTING_POLICY_ENTRY_H_
