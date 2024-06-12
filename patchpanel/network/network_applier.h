// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_NETWORK_NETWORK_APPLIER_H_
#define PATCHPANEL_NETWORK_NETWORK_APPLIER_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/no_destructor.h>
#include <chromeos/net-base/ip_address.h>
#include <chromeos/net-base/network_config.h>
#include <chromeos/net-base/network_priority.h>
#include <chromeos/net-base/proc_fs_stub.h>
#include <chromeos/net-base/rtnl_handler.h>

#include "patchpanel/network/address_service.h"
#include "patchpanel/network/routing_policy_service.h"
#include "patchpanel/network/routing_table.h"

namespace patchpanel {

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

  enum class Technology {
    kEthernet,
    kWiFi,
    kCellular,
    kVPN,
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
  virtual void Start();

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
  virtual void ApplyDNS(net_base::NetworkPriority priority,
                        const std::vector<net_base::IPAddress>& dns_servers,
                        const std::vector<std::string>& dns_search_domains);

  // Apply the routes into per-device routing table. If |gateway| is nullopt,
  // the network is assumed to be point-to-point, and routes are added as
  // on-link.
  virtual void ApplyRoute(
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
  virtual void ApplyRoutingPolicy(
      int interface_index,
      const std::string& interface_name,
      Technology technology,
      net_base::NetworkPriority priority,
      const std::vector<net_base::IPCIDR>& all_addresses,
      const std::vector<net_base::IPv4CIDR>& rfc3442_dsts);

  virtual void ApplyMTU(int interface_index, int mtu);

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

}  // namespace patchpanel

#endif  // PATCHPANEL_NETWORK_NETWORK_APPLIER_H_
