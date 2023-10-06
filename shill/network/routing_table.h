// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NETWORK_ROUTING_TABLE_H_
#define SHILL_NETWORK_ROUTING_TABLE_H_

#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include <base/functional/callback.h>
#include <base/lazy_instance.h>
#include <base/memory/ref_counted.h>
#include <net-base/ip_address.h>
#include <net-base/rtnl_handler.h>
#include <net-base/rtnl_listener.h>
#include <net-base/rtnl_message.h>

#include "shill/network/routing_table_entry.h"

namespace shill {

// This singleton maintains an in-process copy of the routing table on
// a per-interface basis.  It offers the ability for other modules to
// make modifications to the routing table, centered around setting the
// default route for an interface or modifying its metric (priority).
class RoutingTable {
 public:
  // Used to detect default route added by kernel when receiving RA.
  // Note that since 5.18 kernel this value will become configurable through
  // net.ipv6.conf.all.ra_defrtr_metric and we need to be sure this value
  // remains identical with kernel configuration.
  static constexpr int kKernelSlaacRouteMetric = 1024;

  // The metric shill will install its IPv4 default route. Does not have real
  // impact to the routing decision since there will only be one default route
  // in each routing table.
  static constexpr int kShillDefaultRouteMetric = 65536;

  // ID for the routing table that used for CLAT default routes. Patchpanel is
  // responsible for adding and removing routes in this table. Using a user
  // defined table ID lesser than 255 to avoid conflict with per-device table
  // (for which we use table ID 1000+).
  static constexpr uint32_t kClatRoutingTableId = 249;

  // ID for a routing table to block all traffic. Used in b/180521518 to prevent
  // Chrome to send traffic through IPv6 cellular when there is another
  // IPv4-only primary network.
  static constexpr uint32_t kUnreachableTableId = 250;

  virtual ~RoutingTable();

  static RoutingTable* GetInstance();

  virtual void Start();
  virtual void Stop();

  // Informs RoutingTable that a new Device has come up. While RoutingTable
  // could find out about a new Device by seeing a new interface index in a
  // kernel-added route, having this allows for any required setup to occur
  // prior to routes being created for the Device in question.
  void RegisterDevice(int interface_index, const std::string& link_name);

  // Causes RoutingTable to stop managing a particular interface index. This
  // method does not perform clean up that would allow corresponding interface
  // to be used as an unmanaged Device *unless* routes for that interface are
  // re-added. For example, changing accept_ra_rt_table for an interface from -N
  // to 0 will not cause the routes to move back to the main routing table, and
  // in many cases (like a regular link down event for a managed interface), we
  // would not want shill to manually move those routes back.
  void DeregisterDevice(int interface_index, const std::string& link_name);

  // Add an entry to the routing table.
  virtual bool AddRoute(int interface_index, const RoutingTableEntry& entry);
  // Remove an entry from the routing table.
  virtual bool RemoveRoute(int interface_index, const RoutingTableEntry& entry);

  // Get the default route associated with an interface of a given addr family.
  // The route is copied into |*entry|.
  virtual bool GetDefaultRoute(int interface_index,
                               net_base::IPFamily family,
                               RoutingTableEntry* entry);

  // Set the default route for an interface with index |interface_index|,
  // given the IPAddress of the gateway |gateway_address| and priority
  // |metric|.
  virtual bool SetDefaultRoute(int interface_index,
                               const net_base::IPAddress& gateway_address,
                               uint32_t table_id);

  // Create a blackhole route for a given IP family.  Returns true
  // on successfully sending the route request, false otherwise.
  virtual bool CreateBlackholeRoute(int interface_index,
                                    net_base::IPFamily family,
                                    uint32_t metric,
                                    uint32_t table_id);

  // Remove routes associated with interface.
  // Route entries are immediately purged from our copy of the routing table.
  virtual void FlushRoutes(int interface_index);

  // Iterate over all routing tables removing routes tagged with |tag| of IP
  // family |family|. Route entries are immediately purged from our copy of the
  // routing table.
  virtual void FlushRoutesWithTag(int tag, net_base::IPFamily family);

  // Reset local state for this interface.
  virtual void ResetTable(int interface_index);

  static uint32_t GetInterfaceTableId(int interface_index);

 protected:
  RoutingTable();
  RoutingTable(const RoutingTable&) = delete;
  RoutingTable& operator=(const RoutingTable&) = delete;

 private:
  friend base::LazyInstanceTraitsBase<RoutingTable>;
  friend class RoutingTableTest;

  using RouteTableEntryVector = std::vector<RoutingTableEntry>;
  using RouteTables = std::unordered_map<int, RouteTableEntryVector>;

  // Add an entry to the kernel routing table without modifying the internal
  // routing-table bookkeeping.
  bool AddRouteToKernelTable(int interface_index,
                             const RoutingTableEntry& entry);
  // Remove an entry to the kernel routing table without modifying the internal
  // routing-table bookkeeping.
  bool RemoveRouteFromKernelTable(int interface_index,
                                  const RoutingTableEntry& entry);

  void RouteMsgHandler(const net_base::RTNLMessage& msg);
  bool ApplyRoute(uint32_t interface_index,
                  const RoutingTableEntry& entry,
                  net_base::RTNLMessage::Mode mode,
                  unsigned int flags);
  // Get the default route associated with an interface of a given addr family.
  // A pointer to the route is placed in |*entry|.
  virtual bool GetDefaultRouteInternal(int interface_index,
                                       net_base::IPFamily family,
                                       RoutingTableEntry** entry);

  RouteTables tables_;
  std::set<int> managed_interfaces_;

  std::unique_ptr<net_base::RTNLListener> route_listener_;

  // Cache singleton pointer for performance and test purposes.
  net_base::RTNLHandler* rtnl_handler_;
};

}  // namespace shill

#endif  // SHILL_NETWORK_ROUTING_TABLE_H_
