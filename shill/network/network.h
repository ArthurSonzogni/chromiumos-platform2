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
#include "shill/mockable.h"
#include "shill/net/ip_address.h"
#include "shill/network/dhcp_controller.h"
#include "shill/technology.h"

namespace shill {

class DeviceInfo;

// An object of Network class represents a network interface in the kernel, and
// maintains the layer 3 configuration on this interface.
// TODO(b/232177767): Currently this class is mainly a wrapper of the Connection
// class.
class Network {
 public:
  explicit Network(int interface_index,
                   const std::string& interface_name,
                   Technology technology,
                   bool fixed_ip_params,
                   DeviceInfo* device_info);
  Network(const Network&) = delete;
  Network& operator=(const Network&) = delete;
  virtual ~Network() = default;

  // Creates the associated Connection object if not exists.
  void CreateConnection();
  // Destroys the associated Connection object if exists.
  void DestroyConnection();
  // Returns if the associated Connection object exist. Note that the return
  // value does not indicate any real state of the network. This function will
  // finally be removed.
  mockable bool HasConnectionObject() const;

  int interface_index() const { return interface_index_; }
  std::string interface_name() const { return interface_name_; }

  // TODO(b/232177767): Wrappers for the corresponding functions in the
  // Connection class. This is a temporary solution. The caller should guarantee
  // there is a Connection object inside this object.
  void UpdateFromIPConfig(const IPConfig::Properties& config);
  mockable void SetPriority(uint32_t priority, bool is_primary_physical);
  mockable bool IsDefault() const;
  mockable void SetUseDNS(bool enable);
  void UpdateDNSServers(const std::vector<std::string>& dns_servers);
  void UpdateRoutingPolicy();
  mockable std::string GetSubnetName() const;
  bool IsIPv6() const;

  // TODO(b/232177767): Getters for access members in Connection. This is a
  // temporary solution. The caller should guarantee there is a Connection
  // object inside this object.
  mockable const std::vector<std::string>& dns_servers() const;
  mockable const IPAddress& local() const;
  const IPAddress& gateway() const;

  // TODO(b/232177767): Remove once we eliminate all Connection references in
  // shill.
  Connection* connection() const { return connection_.get(); }

  // TODO(b/232177767): This group of getters and setters are only exposed for
  // the purpose of refactor. New code outside Device should not use these.
  DHCPController* dhcp_controller() const { return dhcp_controller_.get(); }
  IPConfig* ipconfig() const { return ipconfig_.get(); }
  IPConfig* ip6config() const { return ip6config_.get(); }
  void set_dhcp_controller(std::unique_ptr<DHCPController> controller) {
    dhcp_controller_ = std::move(controller);
  }
  void set_ipconfig(std::unique_ptr<IPConfig> config) {
    ipconfig_ = std::move(config);
  }
  void set_ip6config(std::unique_ptr<IPConfig> config) {
    ip6config_ = std::move(config);
  }
  bool fixed_ip_params() const { return fixed_ip_params_; }

  // Only used in tests.
  void set_connection_for_testing(std::unique_ptr<Connection> connection) {
    connection_ = std::move(connection);
  }
  void set_fixed_ip_params_for_testing(bool val) { fixed_ip_params_ = val; }

 private:
  const int interface_index_;
  const std::string interface_name_;
  const Technology technology_;

  // If true, IP parameters should not be modified. This should not be changed
  // after a Network object is created. Make it modifiable just for unit tests.
  bool fixed_ip_params_;

  std::unique_ptr<Connection> connection_;

  std::unique_ptr<DHCPController> dhcp_controller_;
  std::unique_ptr<IPConfig> ipconfig_;
  std::unique_ptr<IPConfig> ip6config_;

  // Other dependencies.
  DeviceInfo* device_info_;
};

}  // namespace shill

#endif  // SHILL_NETWORK_NETWORK_H_
