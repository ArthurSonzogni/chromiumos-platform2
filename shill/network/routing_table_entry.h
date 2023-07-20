// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NETWORK_ROUTING_TABLE_ENTRY_H_
#define SHILL_NETWORK_ROUTING_TABLE_ENTRY_H_

#include <linux/rtnetlink.h>

#include <iostream>

#include <net-base/ip_address.h>

namespace shill {

// Represents a single entry in a routing table.
struct RoutingTableEntry {
  static constexpr int kDefaultTag = -1;

  explicit RoutingTableEntry(net_base::IPFamily family);
  RoutingTableEntry(const net_base::IPCIDR& dst_in,
                    const net_base::IPCIDR& src_in,
                    const net_base::IPAddress& gateway_in);

  RoutingTableEntry& SetMetric(uint32_t metric_in);
  RoutingTableEntry& SetScope(unsigned char scope_in);
  RoutingTableEntry& SetTable(uint32_t table_in);
  RoutingTableEntry& SetType(unsigned char type_in);
  RoutingTableEntry& SetTag(int tag_in);

  bool operator==(const RoutingTableEntry& b) const;

  net_base::IPCIDR dst;
  net_base::IPCIDR src;
  net_base::IPAddress gateway;  // RoutingTableEntry uses all-zero gateway
                                // address to represent no gateway.
  uint32_t metric = 0;
  unsigned char scope = RT_SCOPE_UNIVERSE;
  uint32_t table = RT_TABLE_MAIN;
  unsigned char type = RTN_UNICAST;
  unsigned char protocol = RTPROT_BOOT;

  // Connections use their interface index as the tag when adding routes, so
  // that as they are destroyed, they can remove all their dependent routes.
  int tag = kDefaultTag;
};

// Print out an entry in a format similar to that of ip route.
std::ostream& operator<<(std::ostream& os, const RoutingTableEntry& entry);

}  // namespace shill

#endif  // SHILL_NETWORK_ROUTING_TABLE_ENTRY_H_
