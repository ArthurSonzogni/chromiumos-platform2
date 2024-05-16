// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_IPCONFIG_H_
#define SHILL_IPCONFIG_H_

#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include <gtest/gtest_prod.h>
#include <net-base/ip_address.h>
#include <net-base/network_config.h>

#include "shill/network/dhcpv4_config.h"
#include "shill/store/property_store.h"

namespace shill {
class ControlInterface;
class Error;
class IPConfigAdaptorInterface;

class IPConfig {
 public:
  static constexpr int kUndefinedMTU = 0;

  static constexpr char kTypeDHCP[] = "dhcp";

  IPConfig(ControlInterface* control_interface, const std::string& device_name);
  IPConfig(ControlInterface* control_interface,
           const std::string& device_name,
           const std::string& type);
  IPConfig(const IPConfig&) = delete;
  IPConfig& operator=(const IPConfig&) = delete;

  virtual ~IPConfig();

  const std::string& device_name() const { return device_name_; }
  const std::string& type() const { return type_; }
  uint32_t serial() const { return serial_; }

  const RpcIdentifier& GetRpcIdentifier() const;

  uint32_t GetLeaseDurationSeconds(Error* /*error*/);

  PropertyStore* mutable_store() { return &store_; }
  const PropertyStore& store() const { return store_; }

  // Applies the |family| part of |config| and |dhcp_data| to this object and
  // inform D-Bus listeners of the change.
  void ApplyNetworkConfig(
      const net_base::NetworkConfig& config,
      net_base::IPFamily family = net_base::IPFamily::kIPv4,
      const std::optional<DHCPv4Config::Data>& dhcp_data = std::nullopt);

 protected:
  struct Properties {
    Properties();
    ~Properties();

    // Applies all non-empty properties in |network_config| of |family| to this
    // object. The |address_family| on |this| must be either empty or the same
    // as |family|".
    void UpdateFromNetworkConfig(
        const net_base::NetworkConfig& network_config,
        net_base::IPFamily family = net_base::IPFamily::kIPv4);

    void UpdateFromDHCPData(const DHCPv4Config::Data& dhcp_data);

    std::optional<net_base::IPFamily> address_family = std::nullopt;
    std::string address;
    int32_t subnet_prefix = 0;
    std::string broadcast_address;
    std::vector<std::string> dns_servers;
    std::vector<std::string> domain_search;
    std::string gateway;
    std::string method;
    // The address of the remote endpoint for pointopoint interfaces.
    // Note that presence of this field indicates that this is a p2p interface,
    // and a gateway won't be needed in creating routes on this interface.
    std::string peer_address;
    // MTU to set on the interface.  If unset, defaults to |kUndefinedMTU|.
    int32_t mtu = kUndefinedMTU;
    // Informational data from DHCP.
    DHCPv4Config::Data dhcp_data;
  };

  const Properties& properties() const { return properties_; }

 private:
  friend class IPConfigTest;

  friend std::ostream& operator<<(std::ostream& stream, const IPConfig& config);
  friend bool operator==(const Properties& lhs, const Properties& rhs);
  friend std::ostream& operator<<(std::ostream& stream,
                                  const Properties& properties);

  // Inform RPC listeners of changes to our properties. MAY emit
  // changes even on unchanged properties.
  void EmitChanges();

  static uint32_t global_serial_;
  PropertyStore store_;
  const std::string device_name_;
  const std::string type_;
  const uint32_t serial_;
  std::unique_ptr<IPConfigAdaptorInterface> adaptor_;
  Properties properties_;
};

std::ostream& operator<<(std::ostream& stream, const IPConfig& config);

}  // namespace shill

#endif  // SHILL_IPCONFIG_H_
