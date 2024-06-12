// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/network/routing_table_entry.h"

#include <string>

#include <base/strings/stringprintf.h>
#include <chromeos/net-base/ip_address.h>

namespace patchpanel {

RoutingTableEntry::RoutingTableEntry(net_base::IPFamily family)
    : dst(net_base::IPCIDR(family)),
      gateway(net_base::IPAddress(family)),
      pref_src(net_base::IPAddress(family)) {}

// clang-format off
bool RoutingTableEntry::operator==(const RoutingTableEntry& b) const {
  return (dst == b.dst &&
          gateway == b.gateway &&
          pref_src == b.pref_src &&
          metric == b.metric &&
          scope == b.scope &&
          table == b.table &&
          type == b.type &&
          tag == b.tag);
}
// clang-format on

std::ostream& operator<<(std::ostream& os, const RoutingTableEntry& entry) {
  std::string dest_address =
      entry.dst.IsDefault() ? "default" : entry.dst.ToString();
  switch (entry.type) {
    case RTN_LOCAL:
      os << "local " << dest_address;
      break;
    case RTN_BROADCAST:
      os << "broadcast " << dest_address;
      break;
    case RTN_BLACKHOLE:
      os << "blackhole";  // Don't print the address.
      break;
    case RTN_UNREACHABLE:
      os << "unreachable";  // Don't print the address.
      break;
    default:
      os << dest_address;
      break;
  }
  if (!entry.gateway.IsZero()) {
    os << " via " << entry.gateway.ToString();
  }
  if (!entry.pref_src.IsZero()) {
    os << " src " + entry.pref_src.ToString();
  }
  os << " metric " << entry.metric << " "
     << net_base::ToString(entry.dst.GetFamily()) << " table "
     << static_cast<int>(entry.table) << " tag " << entry.tag;
  return os;
}

}  // namespace patchpanel
