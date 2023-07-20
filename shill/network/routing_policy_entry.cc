// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/routing_policy_entry.h"

#include <linux/rtnetlink.h>

#include <utility>

#include <base/strings/stringprintf.h>
#include <net-base/ip_address.h>

bool operator==(const fib_rule_uid_range& a, const fib_rule_uid_range& b) {
  return (a.start == b.start) && (a.end == b.end);
}

namespace shill {

RoutingPolicyEntry::RoutingPolicyEntry(net_base::IPFamily family)
    : family(family),
      dst(net_base::IPCIDR(family)),
      src(net_base::IPCIDR(family)) {}

std::ostream& operator<<(std::ostream& os, const RoutingPolicyEntry& entry) {
  os << "{" << net_base::ToString(entry.family) << " " << entry.priority
     << ": ";
  if (entry.invert_rule) {
    os << "not ";
  }
  os << "from ";
  if (!entry.src.address().IsZero()) {
    os << entry.src.ToString() << " ";
  } else {
    os << "all ";
  }
  if (!entry.dst.address().IsZero()) {
    os << "to " << entry.dst.ToString() << " ";
  }
  if (entry.fw_mark) {
    os << base::StringPrintf("fwmark 0x%08x/0x%08x ", entry.fw_mark->value,
                             entry.fw_mark->mask);
  }
  if (entry.iif_name) {
    os << "iif " << *entry.iif_name << " ";
  }
  if (entry.oif_name) {
    os << "oif " << *entry.oif_name << " ";
  }
  if (entry.uid_range) {
    os << "uidrange " << entry.uid_range->start << "-" << entry.uid_range->end
       << " ";
  }
  os << "lookup " << entry.table << "}";
  return os;
}

// clang-format off
bool RoutingPolicyEntry::operator==(const RoutingPolicyEntry& b) const {
    return (family == b.family &&
            priority == b.priority &&
            table == b.table &&
            dst == b.dst &&
            src == b.src &&
            fw_mark == b.fw_mark &&
            uid_range == b.uid_range &&
            iif_name == b.iif_name &&
            oif_name == b.oif_name &&
            invert_rule == b.invert_rule);
}
// clang-format on

}  // namespace shill
