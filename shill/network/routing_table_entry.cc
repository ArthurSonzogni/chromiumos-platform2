// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/routing_table_entry.h"

#include <string>

#include <base/strings/stringprintf.h>
#include <net-base/ip_address.h>

namespace shill {

RoutingTableEntry::RoutingTableEntry(net_base::IPFamily family)
    : dst(net_base::IPCIDR(family)),
      src(net_base::IPCIDR(family)),
      gateway(net_base::IPAddress(family)) {}

RoutingTableEntry::RoutingTableEntry(const net_base::IPCIDR& dst_in,
                                     const net_base::IPCIDR& src_in,
                                     const net_base::IPAddress& gateway_in)
    : dst(dst_in), src(src_in), gateway(gateway_in) {}

RoutingTableEntry& RoutingTableEntry::SetMetric(uint32_t metric_in) {
  metric = metric_in;
  return *this;
}

RoutingTableEntry& RoutingTableEntry::SetScope(unsigned char scope_in) {
  scope = scope_in;
  return *this;
}

RoutingTableEntry& RoutingTableEntry::SetTable(uint32_t table_in) {
  table = table_in;
  return *this;
}

RoutingTableEntry& RoutingTableEntry::SetType(unsigned char type_in) {
  type = type_in;
  return *this;
}

RoutingTableEntry& RoutingTableEntry::SetTag(int tag_in) {
  tag = tag_in;
  return *this;
}

// clang-format off
bool RoutingTableEntry::operator==(const RoutingTableEntry& b) const {
  return (dst == b.dst &&
          src == b.src &&
          gateway == b.gateway &&
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
  const char* dest_prefix;
  switch (entry.type) {
    case RTN_LOCAL:
      dest_prefix = "local ";
      break;
    case RTN_BROADCAST:
      dest_prefix = "broadcast ";
      break;
    case RTN_BLACKHOLE:
      dest_prefix = "blackhole";
      dest_address = "";  // Don't print the address.
      break;
    case RTN_UNREACHABLE:
      dest_prefix = "unreachable";
      dest_address = "";  // Don't print the address.
      break;
    default:
      dest_prefix = "";
      break;
  }
  std::string gateway;
  if (!entry.gateway.IsZero()) {
    gateway = " via " + entry.gateway.ToString();
  }
  std::string src;
  if (!entry.src.IsDefault()) {
    src = " src " + entry.src.ToString();
  }

  os << base::StringPrintf("%s%s%s metric %d %s table %d tag %d%s", dest_prefix,
                           dest_address.c_str(), gateway.c_str(), entry.metric,
                           net_base::ToString(entry.dst.GetFamily()).c_str(),
                           static_cast<int>(entry.table), entry.tag,
                           src.c_str());
  return os;
}

}  // namespace shill
