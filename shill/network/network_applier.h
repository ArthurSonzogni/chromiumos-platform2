// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NETWORK_NETWORK_APPLIER_H_
#define SHILL_NETWORK_NETWORK_APPLIER_H_

#include <memory>
#include <string>
#include <vector>

#include <base/no_destructor.h>

#include "net-base/ipv4_address.h"
#include "shill/ipconfig.h"
#include "shill/network/network_priority.h"
#include "shill/network/proc_fs_stub.h"
#include "shill/resolver.h"
#include "shill/routing_policy_service.h"
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
      RoutingPolicyService* rule_table,
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

  // Apply the routing policy configuration for a certain interface depending on
  // its |technology| and |priority|. |all_addresses| configured on this
  // interface are needed as information to configure source-IP prefix. If there
  // are any classless static routes configured in DHCPv4, passing destinations
  // of those routes as |rfc3442_dsts| will create routing rules that force
  // per-interface table for those destinations.
  void ApplyRoutingPolicy(int interface_index,
                          const std::string& interface_name,
                          Technology technology,
                          NetworkPriority priority,
                          const std::vector<IPAddress>& all_addresses,
                          const std::vector<net_base::IPv4CIDR>& rfc3442_dsts);

 protected:
  NetworkApplier();
  NetworkApplier(const NetworkApplier&) = delete;
  NetworkApplier& operator=(const NetworkApplier&) = delete;

 private:
  friend class base::NoDestructor<NetworkApplier>;

  // Cache singleton pointers for performance and test purposes.
  Resolver* resolver_;
  RoutingPolicyService* rule_table_;

  // A ProcFsStub instance with no specific interface_name, for the purpose of
  // calling FlushRoutingCache().
  std::unique_ptr<ProcFsStub> proc_fs_;
};

}  // namespace shill

#endif  // SHILL_NETWORK_NETWORK_APPLIER_H_
