// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NETWORK_NETWORK_H_
#define SHILL_NETWORK_NETWORK_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "shill/connection.h"
#include "shill/ipconfig.h"
#include "shill/net/ip_address.h"
#include "shill/technology.h"

namespace shill {

class DeviceInfo;

// An object of Network class represents a network interface in the kernel, and
// maintains the layer 3 configuration on this interface.
// TODO(b/232177767): Currently this class is mainly a wrapper of the Connection
// class.
class Network {
 public:
  Network();
  Network(const Network&) = delete;
  Network& operator=(const Network&) = delete;
  ~Network() = default;

  // Creates the associated Connection object if not exists.
  void CreateConnection(int interface_index,
                        const std::string& interface_name,
                        bool fixed_ip_params,
                        Technology technology,
                        const DeviceInfo* device_info);
  // Destroys the associated Connection object if exists.
  void DestroyConnection();
  // Returns if the associated Connection object exist. Note that the return
  // value does not indicate any real state of the network. This function will
  // finally be removed.
  bool HasConnectionObject() const;

  // TODO(b/232177767): Wrappers for the corresponding functions in the
  // Connection class. This is a temporary solution. The caller should guarantee
  // there is a Connection object inside this object.
  void UpdateFromIPConfig(const IPConfig::Properties& config);
  void SetPriority(uint32_t priority, bool is_primary_physical);
  bool IsDefault() const;
  void SetUseDNS(bool enable);
  void UpdateDNSServers(const std::vector<std::string>& dns_servers);
  void UpdateRoutingPolicy();
  std::string GetSubnetName() const;
  bool IsIPv6() const;

  // TODO(b/232177767): Getters for access members in Connection. This is a
  // temporary solution. The caller should guarantee there is a Connection
  // object inside this object.
  std::string interface_name() const;
  int interface_index() const;
  const std::vector<std::string>& dns_servers() const;
  const IPAddress& local() const;
  const IPAddress& gateway() const;

  // TODO(b/232177767): Remove once we eliminate all Connection references in
  // shill.
  Connection* connection() const { return connection_.get(); }

  // Only used in tests.
  void set_connection_for_testing(std::unique_ptr<Connection> connection) {
    connection_ = std::move(connection);
  }

 private:
  std::unique_ptr<Connection> connection_;
};

}  // namespace shill

#endif  // SHILL_NETWORK_NETWORK_H_
