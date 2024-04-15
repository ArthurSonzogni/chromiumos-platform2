// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/network/routing_table.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/fib_rules.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>  // NOLINT - must be included after netinet/ether.h
#include <net/if_arp.h>
#include <netinet/ether.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <limits>
#include <memory>
#include <string>
#include <utility>
#include "patchpanel/network/routing_table_entry.h"

#include <base/check.h>
#include <base/check_op.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/stl_util.h>
#include <base/strings/stringprintf.h>
#include <brillo/userdb_utils.h>
#include <net-base/byte_utils.h>
#include <net-base/ip_address.h>
#include <net-base/rtnl_handler.h>
#include <net-base/rtnl_listener.h>

namespace patchpanel {

namespace {

// Amount added to an interface index to come up with the routing table ID for
// that interface. Needs to match the kIPFlagPerDeviceRoutingTableForRAEnabled
// offset set in net_base::ProcFsStub.
constexpr int kInterfaceTableIdIncrement = 1000;
static_assert(
    kInterfaceTableIdIncrement > RT_TABLE_LOCAL,
    "kInterfaceTableIdIncrement must be greater than RT_TABLE_LOCAL, "
    "as otherwise some interface's table IDs may collide with system tables.");
}  // namespace

// These don't have named constants in the system header files, but they
// are documented in ip-rule(8) and hardcoded in net/ipv4/fib_rules.c.

RoutingTable::RoutingTable()
    : rtnl_handler_(net_base::RTNLHandler::GetInstance()) {
  VLOG(2) << __func__;
}

RoutingTable::~RoutingTable() = default;

void RoutingTable::Start() {
  VLOG(2) << __func__;

  // Initialize kUnreachableTableId as a table to block traffic.
  auto route = RoutingTableEntry(net_base::IPFamily::kIPv6);
  route.table = kUnreachableTableId;
  route.type = RTN_UNREACHABLE;
  AddRouteToKernelTable(0, route);
  route = RoutingTableEntry(net_base::IPFamily::kIPv4);
  route.table = kUnreachableTableId;
  route.type = RTN_UNREACHABLE;
  AddRouteToKernelTable(0, route);
}

bool RoutingTable::AddRoute(int interface_index,
                            const RoutingTableEntry& entry) {
  // Normal routes (i.e. not blackhole or unreachable) should be sent to a
  // the interface's per-device table.
  if (entry.table != GetInterfaceTableId(interface_index) &&
      entry.type != RTN_BLACKHOLE && entry.type != RTN_UNREACHABLE) {
    LOG(ERROR) << "Can't add route to table " << entry.table
               << " when the interface's per-device table is "
               << GetInterfaceTableId(interface_index);
    return false;
  }

  if (!AddRouteToKernelTable(interface_index, entry)) {
    return false;
  }
  tables_[interface_index].push_back(entry);
  return true;
}

bool RoutingTable::RemoveRoute(int interface_index,
                               const RoutingTableEntry& entry) {
  if (!RemoveRouteFromKernelTable(interface_index, entry)) {
    return false;
  }
  RouteTableEntryVector& table = tables_[interface_index];
  for (auto nent = table.begin(); nent != table.end(); ++nent) {
    if (*nent == entry) {
      table.erase(nent);
      return true;
    }
  }
  LOG(WARNING) << "Successfully removed routing entry but could not find the "
               << "corresponding entry in patchpanel's representation of the "
               << "routing table.";
  return true;
}

bool RoutingTable::GetDefaultRouteInternal(int interface_index,
                                           net_base::IPFamily family,
                                           RoutingTableEntry** entry) {
  VLOG(2) << __func__ << " index " << interface_index << " family " << family;

  RouteTables::iterator table = tables_.find(interface_index);
  if (table == tables_.end()) {
    VLOG(2) << __func__ << " no table";
    return false;
  }

  // If there are multiple defaulte routes choose the one with lowest metric.
  uint32_t lowest_metric = UINT_MAX;
  for (auto& nent : table->second) {
    if (nent.dst.IsDefault() && nent.dst.GetFamily() == family &&
        nent.metric < lowest_metric) {
      *entry = &nent;
      lowest_metric = nent.metric;
    }
  }

  if (lowest_metric == UINT_MAX) {
    VLOG(2) << __func__ << " no route";
    return false;
  } else {
    VLOG(2) << __func__ << ": found" << " gateway "
            << (*entry)->gateway.ToString() << " metric " << (*entry)->metric;
    return true;
  }
}

bool RoutingTable::SetDefaultRoute(int interface_index,
                                   const net_base::IPAddress& gateway_address,
                                   uint32_t table_id) {
  VLOG(2) << __func__ << " index " << interface_index;

  RoutingTableEntry* old_entry;

  if (GetDefaultRouteInternal(interface_index, gateway_address.GetFamily(),
                              &old_entry)) {
    if (old_entry->gateway == gateway_address && old_entry->table == table_id) {
      return true;
    } else {
      if (!RemoveRoute(interface_index, *old_entry)) {
        LOG(WARNING) << "Failed to remove old default route for interface "
                     << interface_index;
      }
    }
  }

  RoutingTableEntry entry(gateway_address.GetFamily());
  entry.gateway = gateway_address;
  entry.metric = kDefaultRouteMetric;
  entry.table = table_id;
  entry.tag = interface_index;
  return AddRoute(interface_index, entry);
}

void RoutingTable::FlushRoutes(int interface_index) {
  VLOG(2) << __func__;

  auto table = tables_.find(interface_index);
  if (table == tables_.end()) {
    return;
  }

  for (const auto& nent : table->second) {
    RemoveRouteFromKernelTable(interface_index, nent);
  }
  table->second.clear();
}

void RoutingTable::FlushRoutesWithTag(int tag, net_base::IPFamily family) {
  VLOG(2) << __func__;

  for (auto& table : tables_) {
    for (auto nent = table.second.begin(); nent != table.second.end();) {
      if (nent->tag == tag && nent->dst.GetFamily() == family) {
        RemoveRouteFromKernelTable(table.first, *nent);
        nent = table.second.erase(nent);
      } else {
        ++nent;
      }
    }
  }
}

void RoutingTable::ResetTable(int interface_index) {
  tables_.erase(interface_index);
}

bool RoutingTable::AddRouteToKernelTable(int interface_index,
                                         const RoutingTableEntry& entry) {
  VLOG(2) << __func__ << ": " << " index " << interface_index << " " << entry;

  return ApplyRoute(interface_index, entry, net_base::RTNLMessage::kModeAdd,
                    NLM_F_CREATE | NLM_F_EXCL);
}

bool RoutingTable::RemoveRouteFromKernelTable(int interface_index,
                                              const RoutingTableEntry& entry) {
  VLOG(2) << __func__ << ": " << " index " << interface_index << " " << entry;

  return ApplyRoute(interface_index, entry, net_base::RTNLMessage::kModeDelete,
                    0);
}

bool RoutingTable::ApplyRoute(int interface_index,
                              const RoutingTableEntry& entry,
                              net_base::RTNLMessage::Mode mode,
                              unsigned int flags) {
  DCHECK(entry.table != RT_TABLE_UNSPEC && entry.table != RT_TABLE_COMPAT)
      << "Attempted to apply route: " << entry;

  VLOG(2) << base::StringPrintf("%s: dst %s index %d mode %d flags 0x%x",
                                __func__, entry.dst.ToString().c_str(),
                                interface_index, mode, flags);

  auto message = std::make_unique<net_base::RTNLMessage>(
      net_base::RTNLMessage::kTypeRoute, mode, NLM_F_REQUEST | flags, 0, 0, 0,
      net_base::ToSAFamily(entry.dst.GetFamily()));
  message->set_route_status(net_base::RTNLMessage::RouteStatus(
      static_cast<uint8_t>(entry.dst.prefix_length()),
      /*src_prefix_in=*/0,
      entry.table < 256 ? static_cast<uint8_t>(entry.table) : RT_TABLE_COMPAT,
      entry.protocol, entry.scope, entry.type, 0));

  message->SetAttribute(RTA_TABLE,
                        net_base::byte_utils::ToBytes<uint32_t>(entry.table));
  message->SetAttribute(RTA_PRIORITY,
                        net_base::byte_utils::ToBytes<uint32_t>(entry.metric));
  if (entry.type != RTN_BLACKHOLE) {
    message->SetAttribute(RTA_DST, entry.dst.address().ToBytes());
  }
  if (!entry.gateway.IsZero()) {
    message->SetAttribute(RTA_GATEWAY, entry.gateway.ToBytes());
  }
  if (!entry.pref_src.IsZero()) {
    message->SetAttribute(RTA_PREFSRC, entry.pref_src.ToBytes());
  }
  if (entry.type == RTN_UNICAST) {
    // Note that RouteMsgHandler will ignore anything without RTA_OIF,
    // because that is how it looks up the |tables_| vector.  But
    // FlushRoutes() and FlushRoutesWithTag() do not care.
    message->SetAttribute(
        RTA_OIF, net_base::byte_utils::ToBytes<int32_t>(interface_index));
  }

  return rtnl_handler_->SendMessage(std::move(message), nullptr);
}

bool RoutingTable::CreateBlackholeRoute(int interface_index,
                                        net_base::IPFamily family,
                                        uint32_t metric,
                                        uint32_t table_id) {
  VLOG(2) << base::StringPrintf("%s: family %s metric %d", __func__,
                                net_base::ToString(family).c_str(), metric);

  auto entry = RoutingTableEntry(family);
  entry.metric = metric;
  entry.table = table_id;
  entry.type = RTN_BLACKHOLE;
  entry.tag = interface_index;
  return AddRoute(interface_index, entry);
}

// static
uint32_t RoutingTable::GetInterfaceTableId(int interface_index) {
  return static_cast<uint32_t>(interface_index + kInterfaceTableIdIncrement);
}

}  // namespace patchpanel
