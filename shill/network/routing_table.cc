// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/routing_table.h"

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

#include "shill/logging.h"

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kRoute;
}  // namespace Logging

namespace {

const char kIpv6ProcPath[] = "/proc/sys/net/ipv6/conf";
// Amount added to an interface index to come up with the routing table ID for
// that interface.
constexpr int kInterfaceTableIdIncrement = 1000;
static_assert(
    kInterfaceTableIdIncrement > RT_TABLE_LOCAL,
    "kInterfaceTableIdIncrement must be greater than RT_TABLE_LOCAL, "
    "as otherwise some interface's table IDs may collide with system tables.");

bool ParseRoutingTableMessage(const net_base::RTNLMessage& message,
                              int* interface_index,
                              RoutingTableEntry* entry) {
  if (message.type() != net_base::RTNLMessage::kTypeRoute ||
      !message.HasAttribute(RTA_OIF)) {
    return false;
  }

  const net_base::RTNLMessage::RouteStatus& route_status =
      message.route_status();

  if (route_status.type != RTN_UNICAST) {
    return false;
  }

  if (route_status.table == RT_TABLE_LOCAL) {
    // Shill does not modify local routes, which are managed by the kernel.
    return false;
  }

  const auto interface_index_i32 =
      net_base::byte_utils::FromBytes<int32_t>(message.GetAttribute(RTA_OIF));
  if (!interface_index_i32) {
    return false;
  }
  *interface_index = *interface_index_i32;

  uint32_t metric = 0;
  if (message.HasAttribute(RTA_PRIORITY)) {
    metric = net_base::byte_utils::FromBytes<uint32_t>(
                 message.GetAttribute(RTA_PRIORITY))
                 .value_or(0);
  }

  // The rtmsg structure [0] has a table id field that is only a single
  // byte. Prior to Linux v2.6, routing table IDs were of type u8. v2.6 changed
  // this so that table IDs were u32s, but the uapi here couldn't
  // change. Instead, a separate RTA_TABLE attribute is used to be able to send
  // a full 32-bit table ID. When the table ID is greater than 255, the
  // rtm_table field is set to RT_TABLE_COMPAT.
  //
  // 0) elixir.bootlin.com/linux/v5.0/source/include/uapi/linux/rtnetlink.h#L206
  uint32_t table;
  if (message.HasAttribute(RTA_TABLE)) {
    table = net_base::byte_utils::FromBytes<uint32_t>(
                message.GetAttribute(RTA_TABLE))
                .value_or(0);
  } else {
    table = route_status.table;
    LOG_IF(WARNING, table == RT_TABLE_COMPAT)
        << "Received RT_TABLE_COMPAT, but message has no RTA_TABLE attribute";
  }

  auto family = net_base::FromSAFamily(message.family());
  if (!family) {
    return false;
  }
  entry->dst = message.GetRtaDst().value_or(net_base::IPCIDR(*family));
  entry->src = message.GetRtaSrc().value_or(net_base::IPCIDR(*family));
  entry->gateway =
      message.GetRtaGateway().value_or(net_base::IPAddress(*family));
  entry->table = table;
  entry->metric = metric;
  entry->scope = route_status.scope;
  entry->protocol = route_status.protocol;
  entry->type = route_status.type;

  return true;
}
}  // namespace

// These don't have named constants in the system header files, but they
// are documented in ip-rule(8) and hardcoded in net/ipv4/fib_rules.c.

RoutingTable::RoutingTable()
    : rtnl_handler_(net_base::RTNLHandler::GetInstance()) {
  SLOG(2) << __func__;
}

RoutingTable::~RoutingTable() = default;

void RoutingTable::Start() {
  SLOG(2) << __func__;

  route_listener_ = std::make_unique<net_base::RTNLListener>(
      net_base::RTNLHandler::kRequestRoute,
      base::BindRepeating(&RoutingTable::RouteMsgHandler,
                          base::Unretained(this)));
  rtnl_handler_->RequestDump(net_base::RTNLHandler::kRequestRoute);

  // Initialize kUnreachableTableId as a table to block traffic.
  auto route = RoutingTableEntry(net_base::IPFamily::kIPv6)
                   .SetTable(kUnreachableTableId)
                   .SetType(RTN_UNREACHABLE);
  AddRouteToKernelTable(0, route);
  route = RoutingTableEntry(net_base::IPFamily::kIPv4)
              .SetTable(kUnreachableTableId)
              .SetType(RTN_UNREACHABLE);
  AddRouteToKernelTable(0, route);
}

void RoutingTable::Stop() {
  SLOG(2) << __func__;

  managed_interfaces_.clear();
  route_listener_.reset();
}

void RoutingTable::RegisterDevice(int interface_index,
                                  const std::string& link_name) {
  if (managed_interfaces_.find(interface_index) != managed_interfaces_.end()) {
    return;
  }

  LOG(INFO) << "Device " << link_name << " registered.";
  managed_interfaces_.insert(interface_index);

  uint32_t table_id = GetInterfaceTableId(interface_index);
  // Move existing entries for this interface to the per-Device table.
  for (auto& nent : tables_[interface_index]) {
    if (nent.table == table_id) {
      continue;
    }
    RoutingTableEntry new_entry = nent;
    new_entry.table = table_id;
    AddRouteToKernelTable(interface_index, new_entry);
    RemoveRouteFromKernelTable(interface_index, nent);
    nent.table = table_id;
  }

  // Set accept_ra_rt_table to -N to cause routes created by the reception of
  // RAs to be sent to the table id (interface_index + N).
  std::string ra_rt_table = std::to_string(-kInterfaceTableIdIncrement);
  auto path = base::FilePath(kIpv6ProcPath)
                  .Append(link_name)
                  .Append("accept_ra_rt_table");
  int str_size = static_cast<int>(ra_rt_table.size());
  if (base::WriteFile(path, ra_rt_table.c_str(), str_size) != str_size) {
    LOG(ERROR) << "Cannot write to " << path.MaybeAsASCII();
  }
}

void RoutingTable::DeregisterDevice(int interface_index,
                                    const std::string& link_name) {
  LOG(INFO) << "Device " << link_name << " deregistered.";
  managed_interfaces_.erase(interface_index);
  // Set accept_ra_rt_table to 0. Note that this will *not* cause routes to be
  // moved back from the per-Device table to the main routing table.
  auto path = base::FilePath(kIpv6ProcPath)
                  .Append(link_name)
                  .Append("accept_ra_rt_table");
  if (!base::PathExists(path)) {
    SLOG(2) << "Cannot write to " << path.MaybeAsASCII()
            << ", likely because the interface has already went down.";
  } else if (base::WriteFile(path, "0", 1) != 1) {
    // Note that there is a potential race condition in which the file exists in
    // the check above but is removed by the time we call WriteFile. In this
    // case, the following error log will be spurious.
    LOG(ERROR) << "Cannot write to " << path.MaybeAsASCII();
  }
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
  SLOG(1) << "Successfully removed routing entry but could not find the "
          << "corresponding entry in shill's representation of the "
          << "routing table.";
  return true;
}

bool RoutingTable::GetDefaultRoute(int interface_index,
                                   net_base::IPFamily family,
                                   RoutingTableEntry* entry) {
  RoutingTableEntry* found_entry;
  bool ret = GetDefaultRouteInternal(interface_index, family, &found_entry);
  if (ret) {
    *entry = *found_entry;
  }
  return ret;
}

bool RoutingTable::GetDefaultRouteInternal(int interface_index,
                                           net_base::IPFamily family,
                                           RoutingTableEntry** entry) {
  SLOG(2) << __func__ << " index " << interface_index << " family " << family;

  RouteTables::iterator table = tables_.find(interface_index);
  if (table == tables_.end()) {
    SLOG(2) << __func__ << " no table";
    return false;
  }

  // For IPv6 the kernel will add a new default route with metric 1024
  // every time it sees a router advertisement (which could happen every
  // couple of seconds).  Ignore these when there is another default route
  // with a lower metric.
  uint32_t lowest_metric = UINT_MAX;
  for (auto& nent : table->second) {
    if (nent.dst.IsDefault() && nent.dst.GetFamily() == family &&
        nent.metric < lowest_metric) {
      *entry = &nent;
      lowest_metric = nent.metric;
    }
  }

  if (lowest_metric == UINT_MAX) {
    SLOG(2) << __func__ << " no route";
    return false;
  } else {
    SLOG(2) << __func__ << ": found" << " gateway "
            << (*entry)->gateway.ToString() << " metric " << (*entry)->metric;
    return true;
  }
}

bool RoutingTable::SetDefaultRoute(int interface_index,
                                   const net_base::IPAddress& gateway_address,
                                   uint32_t table_id) {
  SLOG(2) << __func__ << " index " << interface_index;

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

  const auto default_address = net_base::IPCIDR(gateway_address.GetFamily());

  return AddRoute(
      interface_index,
      RoutingTableEntry(default_address, default_address, gateway_address)
          .SetMetric(kShillDefaultRouteMetric)
          .SetTable(table_id)
          .SetTag(interface_index));
}

void RoutingTable::FlushRoutes(int interface_index) {
  SLOG(2) << __func__;

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
  SLOG(2) << __func__;

  for (auto& table : tables_) {
    for (auto nent = table.second.begin(); nent != table.second.end();) {
      if ((nent->tag == tag && nent->dst.GetFamily() == family) ||
          // b/303315643: Workaround the case that shill-added route being
          // deleted and re-added in the cache by RouteMsgHandler. Those routes
          // will have tag -1, but we know all IPv4 routes in device-specific
          // table are added by shill and should be treated as having tag ==
          // ifindex.
          (table.first == tag && family == net_base::IPFamily::kIPv4 &&
           nent->dst.GetFamily() == net_base::IPFamily::kIPv4)) {
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
  SLOG(2) << __func__ << ": " << " index " << interface_index << " " << entry;

  return ApplyRoute(interface_index, entry, net_base::RTNLMessage::kModeAdd,
                    NLM_F_CREATE | NLM_F_EXCL);
}

bool RoutingTable::RemoveRouteFromKernelTable(int interface_index,
                                              const RoutingTableEntry& entry) {
  SLOG(2) << __func__ << ": " << " index " << interface_index << " " << entry;

  return ApplyRoute(interface_index, entry, net_base::RTNLMessage::kModeDelete,
                    0);
}

void RoutingTable::RouteMsgHandler(const net_base::RTNLMessage& message) {
  int interface_index;
  // Initialize it to IPv4, will be set to the real value in
  // ParseRoutingTableMessage().
  RoutingTableEntry entry(net_base::IPFamily::kIPv4);
  if (!ParseRoutingTableMessage(message, &interface_index, &entry)) {
    return;
  }

  if (entry.protocol == RTPROT_RA) {
    // The kernel sends one of these messages pretty much every time it
    // connects to another IPv6 host.  The only interesting message is the
    // one containing the default gateway.
    if (!entry.dst.IsDefault()) {
      return;
    }
  } else if (entry.protocol != RTPROT_BOOT) {
    // Responses to route queries come back with a protocol of
    // RTPROT_UNSPEC.  Otherwise, normal route updates that we are
    // interested in come with a protocol of RTPROT_BOOT.
    return;
  }

  SLOG(2) << __func__ << " "
          << net_base::RTNLMessage::ModeToString(message.mode())
          << " index: " << interface_index << " entry: " << entry;

  bool entry_exists = false;
  bool is_managed = (managed_interfaces_.count(interface_index) != 0);
  uint32_t target_table = GetInterfaceTableId(interface_index);
  // Routes that make it here are either:
  //   * Default routes of protocol RTPROT_RA (most notably, kernel-created IPv6
  //      default routes in response to receiving IPv6 RAs).
  //   * Routes of protocol RTPROT_BOOT, which includes default routes created
  //      by the kernel when an interface comes up and routes created by `ip
  //      route` that do not explicitly specify a different protocol.
  //
  // Thus a different service could create routes that are "hidden" from Shill
  // by using a different protocol value (anything greater than RTPROT_STATIC
  // would be appropriate), while routes created with protocol RTPROT_BOOT will
  // be tracked by Shill. In the future, each service could use a unique
  // protocol value, such that Shill would be able to determine which service
  // created a particular route.
  RouteTableEntryVector& table = tables_[interface_index];
  for (auto nent = table.begin(); nent != table.end();) {
    // clang-format off
    if (nent->dst != entry.dst ||
        nent->src != entry.src ||
        nent->gateway != entry.gateway ||
        nent->scope != entry.scope ||
        nent->metric != entry.metric ||
        nent->type != entry.type) {
      ++nent;
      continue;
    }
    // clang-format on

    if (message.mode() == net_base::RTNLMessage::kModeAdd &&
        (is_managed || entry.table == nent->table)) {
      // Set this to true to avoid adding the same route twice to
      // tables_[interface_index].
      entry_exists = true;
      break;
    }

    if (message.mode() == net_base::RTNLMessage::kModeDelete &&
        entry.table == nent->table) {
      // Keep track of route deletions that come from outside of shill. Continue
      // the loop for resilience to any failure scenario in which
      // tables_[interface_index] has duplicate entries.
      nent = table.erase(nent);
    } else {
      ++nent;
    }
  }

  if (message.mode() != net_base::RTNLMessage::kModeAdd) {
    return;
  }

  // We do not want normal entries for a managed interface to be added to any
  // table but the per-Device routing table. Thus we remove the added route here
  // and re-add it to the per-Device routing table.
  if (is_managed && entry.table != target_table && entry.type == RTN_UNICAST) {
    RoutingTableEntry oldEntry(entry);
    entry.table = target_table;
    ApplyRoute(interface_index, entry, net_base::RTNLMessage::kModeAdd,
               NLM_F_CREATE | NLM_F_REPLACE);
    RemoveRouteFromKernelTable(interface_index, oldEntry);
  }

  if (!entry_exists) {
    table.push_back(entry);
  }
}

bool RoutingTable::ApplyRoute(int interface_index,
                              const RoutingTableEntry& entry,
                              net_base::RTNLMessage::Mode mode,
                              unsigned int flags) {
  DCHECK(entry.table != RT_TABLE_UNSPEC && entry.table != RT_TABLE_COMPAT)
      << "Attempted to apply route: " << entry;

  SLOG(2) << base::StringPrintf("%s: dst %s src %s index %d mode %d flags 0x%x",
                                __func__, entry.dst.ToString().c_str(),
                                entry.src.ToString().c_str(), interface_index,
                                mode, flags);

  auto message = std::make_unique<net_base::RTNLMessage>(
      net_base::RTNLMessage::kTypeRoute, mode, NLM_F_REQUEST | flags, 0, 0, 0,
      net_base::ToSAFamily(entry.dst.GetFamily()));
  message->set_route_status(net_base::RTNLMessage::RouteStatus(
      static_cast<uint8_t>(entry.dst.prefix_length()),
      static_cast<uint8_t>(entry.src.prefix_length()),
      entry.table < 256 ? static_cast<uint8_t>(entry.table) : RT_TABLE_COMPAT,
      entry.protocol, entry.scope, entry.type, 0));

  message->SetAttribute(RTA_TABLE,
                        net_base::byte_utils::ToBytes<uint32_t>(entry.table));
  message->SetAttribute(RTA_PRIORITY,
                        net_base::byte_utils::ToBytes<uint32_t>(entry.metric));
  if (entry.type != RTN_BLACKHOLE) {
    message->SetAttribute(RTA_DST, entry.dst.address().ToBytes());
  }
  if (!entry.src.IsDefault()) {
    message->SetAttribute(RTA_SRC, entry.src.address().ToBytes());
  }
  if (!entry.gateway.IsZero()) {
    message->SetAttribute(RTA_GATEWAY, entry.gateway.ToBytes());
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
  SLOG(2) << base::StringPrintf("%s: family %s metric %d", __func__,
                                net_base::ToString(family).c_str(), metric);

  auto entry = RoutingTableEntry(family)
                   .SetMetric(metric)
                   .SetTable(table_id)
                   .SetType(RTN_BLACKHOLE)
                   .SetTag(interface_index);
  return AddRoute(interface_index, entry);
}

// static
uint32_t RoutingTable::GetInterfaceTableId(int interface_index) {
  return static_cast<uint32_t>(interface_index + kInterfaceTableIdIncrement);
}

}  // namespace shill
