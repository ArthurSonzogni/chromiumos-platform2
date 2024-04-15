// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_NETWORK_ROUTING_TABLE_ENTRY_H_
#define PATCHPANEL_NETWORK_ROUTING_TABLE_ENTRY_H_

#include <linux/rtnetlink.h>

#include <iostream>

#include <net-base/ip_address.h>

namespace patchpanel {

// Represents a single entry in a routing table.
struct RoutingTableEntry {
  static constexpr int kDefaultTag = -1;

  explicit RoutingTableEntry(net_base::IPFamily family);

  bool operator==(const RoutingTableEntry& b) const;

  net_base::IPCIDR dst;
  net_base::IPAddress gateway;   // RoutingTableEntry uses all-zero gateway
                                 // address to represent no gateway.
  net_base::IPAddress pref_src;  // The source IP preferred when sending packet
                                 // through this route. i.e. "src" in iproute2.
                                 // All-zero means no specified source IP.
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

}  // namespace patchpanel

#endif  // PATCHPANEL_NETWORK_ROUTING_TABLE_ENTRY_H_
