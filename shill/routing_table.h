// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_ROUTING_TABLE_H_
#define SHILL_ROUTING_TABLE_H_

#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <base/functional/callback.h>
#include <base/lazy_instance.h>
#include <base/memory/ref_counted.h>
#include <net-base/ip_address.h>

#include "shill/net/ip_address.h"
#include "shill/net/rtnl_message.h"
#include "shill/refptr_types.h"
#include "shill/routing_table_entry.h"

namespace shill {

class RTNLHandler;
class RTNLListener;

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
                               IPAddress::Family family,
                               RoutingTableEntry* entry);

  // Get the default IPv6 route associated with an interface which was created
  // by the kernel. The route is copied into |*entry|.
  virtual bool GetDefaultRouteFromKernel(int interface_index,
                                         RoutingTableEntry* entry);

  // Set the default route for an interface with index |interface_index|,
  // given the IPAddress of the gateway |gateway_address| and priority
  // |metric|.
  virtual bool SetDefaultRoute(int interface_index,
                               const IPAddress& gateway_address,
                               uint32_t table_id);

  // Create a blackhole route for a given IP family.  Returns true
  // on successfully sending the route request, false otherwise.
  virtual bool CreateBlackholeRoute(int interface_index,
                                    IPAddress::Family family,
                                    uint32_t metric,
                                    uint32_t table_id);

  // Create a route to a link-attached remote host.  |remote_address|
  // must be directly reachable from |local_address|.  Returns true
  // on successfully sending the route request, false otherwise.
  virtual bool CreateLinkRoute(int interface_index,
                               const IPAddress& local_address,
                               const IPAddress& remote_address,
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

  void RouteMsgHandler(const RTNLMessage& msg);
  bool ApplyRoute(uint32_t interface_index,
                  const RoutingTableEntry& entry,
                  RTNLMessage::Mode mode,
                  unsigned int flags);
  // Get the default route associated with an interface of a given addr family.
  // A pointer to the route is placed in |*entry|.
  virtual bool GetDefaultRouteInternal(int interface_index,
                                       IPAddress::Family family,
                                       RoutingTableEntry** entry);

  RouteTables tables_;
  std::set<int> managed_interfaces_;

  std::unique_ptr<RTNLListener> route_listener_;

  // Cache singleton pointer for performance and test purposes.
  RTNLHandler* rtnl_handler_;
};

}  // namespace shill

#endif  // SHILL_ROUTING_TABLE_H_
