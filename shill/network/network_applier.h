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
#include <net-base/network_config.h>
#include <net-base/network_priority.h>
#include <net-base/proc_fs_stub.h>
#include <net-base/rtnl_handler.h>

#include "shill/mockable.h"
#include "shill/network/address_service.h"
#include "shill/network/routing_policy_service.h"
#include "shill/network/routing_table.h"
#include "shill/technology.h"

namespace shill {

// A singleton class that provide stateless API for Networks to apply their
// configurations into kernel netdevice, routing table, routing policy table,
// and other components implementing network stack.
class NetworkApplier {
 public:
  enum class Area : uint32_t {
    kNone = 0,
    kIPv4Address = 1u << 0,
    kIPv4Route = 1u << 1,
    kIPv4DefaultRoute = 1u << 2,
    kIPv6Address = 1u << 8,
    kIPv6Route = 1u << 9,
    kIPv6DefaultRoute = 1u << 10,
    kRoutingPolicy = 1u << 16,
    kDNS = 1u << 17,
    kMTU = 1u << 18,
    kClear = 1u << 31,  // Clear all old configurations regardless of area
  };

  virtual ~NetworkApplier();

  // Singleton accessor.
  static NetworkApplier* GetInstance();

  // Helper factory function for test code with dependency injection.
  static std::unique_ptr<NetworkApplier> CreateForTesting(
      std::unique_ptr<RoutingTable> routing_table,
      std::unique_ptr<RoutingPolicyService> rule_table,
      std::unique_ptr<AddressService> address_service,
      net_base::RTNLHandler* rtnl_handler,
      std::unique_ptr<net_base::ProcFsStub> proc_fs);

  // Start the RTNL listeners in subcomponents.
  mockable void Start();

  // Notify NetworkApplier before a network interface can be configured by it so
  // it can do proper initialization.
  void Register(int interface_index, const std::string& interface_name);

  // Notify NetworkApplier that a network interface no longer needs to be
  // concerned.
  void Release(int interface_index, const std::string& interface_name);

  // Clear all configurations applied to a certain interface.
  void Clear(int interface_index);

  void ApplyNetworkConfig(int interface_index,
                          const std::string& interface_name,
                          Area area,
                          const net_base::NetworkConfig& network_config,
                          net_base::NetworkPriority priority,
                          Technology technology);

  // Apply the DNS configuration by writing into /etc/resolv.conf.
  // TODO(b/259354228): dnsproxy will take the ownership of resolv.conf file
  // after b/207657239 is resolved.
  mockable void ApplyDNS(net_base::NetworkPriority priority,
                         const std::vector<net_base::IPAddress>& dns_servers,
                         const std::vector<std::string>& dns_search_domains);

  // Apply the local address onto kernel netdevice with interface index
  // |interface_index|. If IPv4, a customized |broadcast| address can be
  // specified.
  // TODO(b/264963034): Multiple IPv6 addresses is currently not supported.
  mockable void ApplyAddress(
      int interface_index,
      const net_base::IPCIDR& local,
      const std::optional<net_base::IPv4Address>& broadcast);

  // Apply the routes into per-device routing table. If |gateway| is nullopt,
  // the network is assumed to be point-to-point, and routes are added as
  // on-link.
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
  mockable void ApplyRoutingPolicy(
      int interface_index,
      const std::string& interface_name,
      Technology technology,
      net_base::NetworkPriority priority,
      const std::vector<net_base::IPCIDR>& all_addresses,
      const std::vector<net_base::IPv4CIDR>& rfc3442_dsts);

  mockable void ApplyMTU(int interface_index, int mtu);

 protected:
  NetworkApplier();
  NetworkApplier(const NetworkApplier&) = delete;
  NetworkApplier& operator=(const NetworkApplier&) = delete;

 private:
  friend class base::NoDestructor<NetworkApplier>;

  std::unique_ptr<RoutingPolicyService> rule_table_;
  std::unique_ptr<RoutingTable> routing_table_;
  std::unique_ptr<AddressService> address_service_;

  // Cache singleton pointers for performance and test purposes.
  net_base::RTNLHandler* rtnl_handler_;

  // A net_base::ProcFsStub instance with no specific interface_name, for the
  // purpose of calling FlushRoutingCache().
  std::unique_ptr<net_base::ProcFsStub> proc_fs_;
};

inline uint32_t operator&(NetworkApplier::Area a, NetworkApplier::Area b) {
  return static_cast<uint32_t>(a) & static_cast<uint32_t>(b);
}

inline NetworkApplier::Area operator|(NetworkApplier::Area a,
                                      NetworkApplier::Area b) {
  return static_cast<NetworkApplier::Area>(static_cast<uint32_t>(a) |
                                           static_cast<uint32_t>(b));
}

inline NetworkApplier::Area& operator|=(NetworkApplier::Area& a,
                                        NetworkApplier::Area b) {
  return a = a | b;
}

}  // namespace shill

#endif  // SHILL_NETWORK_NETWORK_APPLIER_H_
