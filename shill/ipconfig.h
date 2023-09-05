// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_IPCONFIG_H_
#define SHILL_IPCONFIG_H_

#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include <net-base/ip_address.h>

#include "shill/mockable.h"
#include "shill/network/dhcpv4_config.h"
#include "shill/network/network_config.h"
#include "shill/store/property_store.h"

namespace shill {
class ControlInterface;
class Error;
class IPConfigAdaptorInterface;

class IPConfig {
 public:
  struct Route {
    Route() {}
    Route(const std::string& host_in,
          int prefix_in,
          const std::string& gateway_in)
        : host(host_in), prefix(prefix_in), gateway(gateway_in) {}
    std::string host;
    int prefix = 0;
    std::string gateway;
  };

  struct Properties {
    Properties();
    ~Properties();

    // Whether this struct contains both IP address and DNS, and thus is ready
    // to be used for network connection.
    bool HasIPAddressAndDNS() const;

    // Generate a NetworkConfig from an IPConfig::Properties for IPv4 and
    // another one from IPv6. Non-family-specific fields are merged.
    static NetworkConfig ToNetworkConfig(const Properties* ipv4_prop,
                                         const Properties* ipv6_prop);

    // Applies all non-empty properties in |network_config| of |family| to this
    // object. The |address_family| on |this| must be either empty or the same
    // as |family|". When |force_overwrite| is false, field will be kept
    // unchanged if the corresponding field in |network_config| is empty.
    void UpdateFromNetworkConfig(
        const NetworkConfig& network_config,
        bool force_overwrite,
        net_base::IPFamily family = net_base::IPFamily::kIPv4);

    void UpdateFromDHCPData(const DHCPv4Config::Data& dhcp_data);

    std::optional<net_base::IPFamily> address_family = std::nullopt;
    std::string address;
    int32_t subnet_prefix = 0;
    std::string broadcast_address;
    std::vector<std::string> dns_servers;
    std::string domain_name;
    std::vector<std::string> domain_search;
    std::string gateway;
    std::string method;
    // The address of the remote endpoint for pointopoint interfaces.
    // Note that presence of this field indicates that this is a p2p interface,
    // and a gateway won't be needed in creating routes on this interface.
    std::string peer_address;
    // Set the flag to true when the interface should be set as the default
    // route. This flag only affects IPv4.
    bool default_route = true;
    // A list of IP blocks in CIDR format that should be included on this
    // network.
    std::vector<std::string> inclusion_list;
    // A list of IP blocks in CIDR format that should be excluded from VPN.
    std::vector<std::string> exclusion_list;
    // Block IPv6 traffic.  Used if connected to an IPv4-only VPN.
    bool blackhole_ipv6 = false;
    // MTU to set on the interface.  If unset, defaults to |kUndefinedMTU|.
    int32_t mtu = kUndefinedMTU;
    // Routes configured by the classless static routes option in DHCP. Traffic
    // sent to prefixes in this list will be routed through this connection,
    // even if it is not the default connection.
    std::vector<Route> dhcp_classless_static_routes;
    // Informational data from DHCP.
    DHCPv4Config::Data dhcp_data;
  };

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

  void set_properties(const Properties& props) { properties_ = props; }

  // Update DNS servers setting for this ipconfig, this allows Chrome
  // to retrieve the new DNS servers.
  mockable void UpdateDNSServers(std::vector<std::string> dns_servers);

  // Reset the IPConfig properties to their default values.
  mockable void ResetProperties();

  // Updates the IP configuration properties and notifies listeners on D-Bus.
  void UpdateProperties(const Properties& properties);

  PropertyStore* mutable_store() { return &store_; }
  const PropertyStore& store() const { return store_; }

  // Applies |config| to this object and inform D-Bus listeners of the change.
  // When |force_overwrite| is false, field will be kept unchanged if the
  // corresponding field in |network_config| is empty. This is used in scenarios
  // such as combining IP address from static IP config with DNS from DHCP.
  void ApplyNetworkConfig(
      const NetworkConfig& config,
      bool force_overwrite,
      net_base::IPFamily family = net_base::IPFamily::kIPv4);

  // Update all information from DHCP and inform D-Bus listeners of the change.
  void UpdateFromDHCP(const NetworkConfig& config,
                      const DHCPv4Config::Data& dhcp_data);

 protected:
  mockable const Properties& properties() const { return properties_; }

 private:
  friend class IPConfigTest;
  // TODO(b/269401899): Remove all usage of properties() in Network.
  friend class Network;
  friend std::ostream& operator<<(std::ostream& stream, const IPConfig& config);

  // Inform RPC listeners of changes to our properties. MAY emit
  // changes even on unchanged properties.
  mockable void EmitChanges();

  static uint32_t global_serial_;
  PropertyStore store_;
  const std::string device_name_;
  const std::string type_;
  const uint32_t serial_;
  std::unique_ptr<IPConfigAdaptorInterface> adaptor_;
  Properties properties_;
};

bool operator==(const IPConfig::Route& lhs, const IPConfig::Route& rhs);
bool operator==(const IPConfig::Properties& lhs,
                const IPConfig::Properties& rhs);
std::ostream& operator<<(std::ostream& stream, const IPConfig& config);
std::ostream& operator<<(std::ostream& stream,
                         const IPConfig::Properties& properties);

}  // namespace shill

#endif  // SHILL_IPCONFIG_H_
