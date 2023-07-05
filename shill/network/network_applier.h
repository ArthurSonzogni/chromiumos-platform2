// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NETWORK_NETWORK_APPLIER_H_
#define SHILL_NETWORK_NETWORK_APPLIER_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/no_destructor.h>
#include <net-base/ip_address.h>

#include "shill/ipconfig.h"
#include "shill/mockable.h"
#include "shill/net/rtnl_handler.h"
#include "shill/network/address_service.h"
#include "shill/network/network_priority.h"
#include "shill/network/proc_fs_stub.h"
#include "shill/resolver.h"
#include "shill/routing_policy_service.h"
#include "shill/routing_table.h"
#include "shill/technology.h"

namespace shill {

// A singleton class that provide stateless API for Networks to apply their
// configurations into kernel netdevice, routing table, routing policy table,
// and other components implementing network stack.
class NetworkApplier {
 public:
  virtual ~NetworkApplier();

  // Singleton accessor.
  static NetworkApplier* GetInstance();

  // Helper factory function for test code with dependency injection.
  static std::unique_ptr<NetworkApplier> CreateForTesting(
      Resolver* resolver,
      RoutingTable* routing_table,
      RoutingPolicyService* rule_table,
      AddressService* address_service,
      RTNLHandler* rtnl_handler,
      std::unique_ptr<ProcFsStub> proc_fs);

  // Clear all configurations applied to a certain interface.
  void Clear(int interface_index);

  // Apply the DNS configuration by writing into /etc/resolv.conf.
  // TODO(b/259354228): dnsproxy will take the ownership of resolv.conf file
  // after b/207657239 is resolved.
  // TODO(b/269401899): Use NetworkConfig as parameter.
  void ApplyDNS(NetworkPriority priority,
                const IPConfig::Properties* ipv4_properties,
                const IPConfig::Properties* ipv6_properties);

  // Apply the local address onto kernel netdevice with interface index
  // |interface_index|. If IPv4, a customized |broadcast| address can be
  // specified.
  // TODO(b/264963034): Current version only applies a single IP family one
  // time. Modify the call so that it configures IPv4 and IPv6 together.
  // TODO(b/264963034): Multiple IPv6 addresses is currently not supported.
  mockable void ApplyAddress(
      int interface_index,
      const net_base::IPCIDR& local,
      const std::optional<net_base::IPv4Address>& broadcast);

  // Apply the routes into per-device routing table. If |gateway| is nullopt,
  // the network is assumed to be point-to-point, and routes are added as
  // on-link.
  // TODO(b/264963034): Current version only applies a single IP family one
  // time. Modify the call so that it configures IPv4 and IPv6 together.
  mockable void ApplyRoute(
      int interface_index,
      net_base::IPFamily family,
      const std::optional<net_base::IPAddress>& gateway,
      bool fix_gateway_reachability,
      bool default_route,
      bool blackhole_ipv6,
      const std::vector<net_base::IPCIDR>& excluded_routes,
      const std::vector<net_base::IPCIDR>& included_routes,
      const std::vector<std::pair<net_base::IPv4CIDR, net_base::IPv4Address>>&
          rfc3442_routes);

  // Apply the routing policy configuration for a certain interface depending
  // on its |technology| and |priority|. |all_addresses| configured on this
  // interface are needed as information to configure source-IP prefix. If
  // there are any classless static routes configured in DHCPv4, passing
  // destinations of those routes as |rfc3442_dsts| will create routing rules
  // that force per-interface table for those destinations.
  void ApplyRoutingPolicy(int interface_index,
                          const std::string& interface_name,
                          Technology technology,
                          NetworkPriority priority,
                          const std::vector<net_base::IPCIDR>& all_addresses,
                          const std::vector<net_base::IPv4CIDR>& rfc3442_dsts);

  mockable void ApplyMTU(int interface_index, int mtu);

 protected:
  NetworkApplier();
  NetworkApplier(const NetworkApplier&) = delete;
  NetworkApplier& operator=(const NetworkApplier&) = delete;

 private:
  friend class base::NoDestructor<NetworkApplier>;

  // Cache singleton pointers for performance and test purposes.
  // TODO(b/264963034): Let NetworkApplier own those services after external
  // dependencies on them are removed.
  Resolver* resolver_;
  RoutingPolicyService* rule_table_;
  RoutingTable* routing_table_;
  AddressService* address_service_;
  RTNLHandler* rtnl_handler_;

  // A ProcFsStub instance with no specific interface_name, for the purpose of
  // calling FlushRoutingCache().
  std::unique_ptr<ProcFsStub> proc_fs_;
};

}  // namespace shill

#endif  // SHILL_NETWORK_NETWORK_APPLIER_H_
