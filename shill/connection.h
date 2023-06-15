// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_CONNECTION_H_
#define SHILL_CONNECTION_H_

#include <limits>
#include <map>
#include <string>
#include <vector>

#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#include "shill/ipconfig.h"
#include "shill/net/ip_address.h"
#include "shill/network/network_priority.h"
#include "shill/routing_table.h"
#include "shill/technology.h"

namespace shill {

class RTNLHandler;
class Resolver;
class RoutingTable;

// The Connection maintains the implemented state of an IPConfig.
// TODO(b/264963034): in progress of migrating to NetworkApplier. Currently
// Connection maintains IPv4 address and routes (not including routing
// policies).

class Connection {
 public:
  Connection(int interface_index,
             const std::string& interface_name,
             bool fixed_ip_params,
             Technology technology);
  Connection(const Connection&) = delete;
  Connection& operator=(const Connection&) = delete;
  virtual ~Connection();

  // Add the contents of an IPConfig::Properties to the list of managed state.
  // This will replace all previous state for this address family. When
  // properties.method == kTypeIPv6, that means that the address is fronm SLAAC
  // therefore address configuration is skipped and Connection only do routing
  // policy setup.
  virtual void UpdateFromIPConfig(const IPConfig::Properties& properties);

  // Return true if this is an IPv6 connection.
  virtual bool IsIPv6();

  virtual const std::string& interface_name() const { return interface_name_; }

 private:
  friend class ConnectionTest;

  // Create a link route to the gateway when the gateway is in a separate
  // subnet. This can work if the host LAN and gateway LAN are bridged together,
  // but is not a recommended network configuration. Return true if |gateway| is
  // reachable or the function successfully installed the route, and false if
  // |gateway| does not exist or the installation failed.
  bool FixGatewayReachability(const IPAddress& local,
                              const std::optional<IPAddress>& gateway);
  // Allow for the routes specified in |properties.routes| to be served by this
  // connection.
  bool SetupIncludedRoutes(const IPConfig::Properties& properties,
                           bool ignore_gateway);
  // Ensure the destination subnets specified in |properties.exclusion_list|
  // will not be served by this connection.
  bool SetupExcludedRoutes(const IPConfig::Properties& properties);
  void SetMTU(int32_t mtu);

  int interface_index_;
  const std::string interface_name_;
  Technology technology_;

  // Cache for the addresses added earlier by Connection. Note that current
  // Connection implementation only supports adding at most one IPv4 and one
  // IPv6 address.
  std::map<IPAddress::Family, IPAddress> added_addresses_;

  // Do not reconfigure the IP addresses, subnet mask, broadcast, etc.
  bool fixed_ip_params_;
  uint32_t table_id_;
  IPAddress local_;
  IPAddress gateway_;

  // Store cached copies of singletons for speed/ease of testing
  RoutingTable* routing_table_;
  RTNLHandler* rtnl_handler_;
};

}  // namespace shill

#endif  // SHILL_CONNECTION_H_
