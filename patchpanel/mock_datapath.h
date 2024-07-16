// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_MOCK_DATAPATH_H_
#define PATCHPANEL_MOCK_DATAPATH_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <chromeos/net-base/mac_address.h>
#include <gmock/gmock.h>

#include "patchpanel/datapath.h"
#include "patchpanel/iptables.h"
#include "patchpanel/shill_client.h"

namespace patchpanel {

// ARC networking data path configuration utility.
class MockDatapath : public Datapath {
 public:
  MockDatapath(MinijailedProcessRunner* process_runner, System* system);
  MockDatapath(const MockDatapath&) = delete;
  MockDatapath& operator=(const MockDatapath&) = delete;

  ~MockDatapath() override;

  MOCK_METHOD(bool,
              NetnsAttachName,
              (std::string_view netns_name, pid_t netns_pid),
              (override));
  MOCK_METHOD(bool, NetnsDeleteName, (std::string_view netns_name), (override));

  MOCK_METHOD(bool,
              AddBridge,
              (std::string_view ifname, const net_base::IPv4CIDR& cidr),
              (override));
  MOCK_METHOD(void, RemoveBridge, (std::string_view ifname), (override));
  MOCK_METHOD(bool,
              AddToBridge,
              (std::string_view br_ifname, std::string_view ifname),
              (override));

  MOCK_METHOD(std::string,
              AddTunTap,
              (std::string_view name,
               const std::optional<net_base::MacAddress>& mac_addr,
               const std::optional<net_base::IPv4CIDR>& ipv4_cidr,
               std::string_view user,
               DeviceMode dev_mode),
              (override));
  MOCK_METHOD(void,
              RemoveTunTap,
              (std::string_view ifname, DeviceMode dev_mode),
              (override));
  MOCK_METHOD(bool,
              ConnectVethPair,
              (pid_t pid,
               std::string_view netns_name,
               std::string_view veth_ifname,
               std::string_view peer_ifname,
               net_base::MacAddress remote_mac_addr,
               const net_base::IPv4CIDR& remote_ipv4_cidr,
               const std::optional<net_base::IPv6CIDR>& remote_ipv6_cidr,
               bool remote_multicast_flag,
               bool up),
              (override));
  MOCK_METHOD(void, RemoveInterface, (std::string_view ifname), (override));
  MOCK_METHOD(void,
              StartRoutingDevice,
              (const ShillClient::Device& shill_device,
               std::string_view int_ifname,
               TrafficSource source,
               bool static_ipv6),
              (override));
  MOCK_METHOD(void,
              StartRoutingDeviceAsUser,
              (std::string_view int_ifname,
               TrafficSource source,
               const net_base::IPv4Address& int_ipv4_addr,
               std::optional<net_base::IPv4Address> peer_ipv4_addr,
               std::optional<net_base::IPv6Address> int_ipv6_addr,
               std::optional<net_base::IPv6Address> peer_ipv6_addr),
              (override));
  MOCK_METHOD(void,
              StopRoutingDevice,
              (std::string_view int_ifname, TrafficSource source),
              (override));
  MOCK_METHOD(bool,
              MaskInterfaceFlags,
              (std::string_view ifname, uint16_t on, uint16_t off),
              (override));
  MOCK_METHOD(bool,
              AddIPv4RouteToTable,
              (std::string_view ifname,
               const net_base::IPv4CIDR& ipv4_cidr,
               int table_id),
              (override));
  MOCK_METHOD(void,
              DeleteIPv4RouteFromTable,
              (std::string_view ifname,
               const net_base::IPv4CIDR& ipv4_cidr,
               int table_id),
              (override));
  MOCK_METHOD(bool,
              AddIPv4Route,
              (const net_base::IPv4Address& gateway_addr,
               const net_base::IPv4CIDR& cidr),
              (override));
  MOCK_METHOD(bool,
              SetConntrackHelpers,
              (const bool enable_helpers),
              (override));
  MOCK_METHOD(bool,
              SetRouteLocalnet,
              (std::string_view ifname, const bool enable),
              (override));
  MOCK_METHOD(std::string,
              DumpIptables,
              (IpFamily family, Iptables::Table table),
              (override));
  MOCK_METHOD(bool,
              ModprobeAll,
              (const std::vector<std::string>& modules),
              (override));
  MOCK_METHOD(void,
              AddInboundIPv4DNAT,
              (AutoDNATTarget auto_dnat_target,
               const ShillClient::Device& shill_device,
               const net_base::IPv4Address& ipv4_addr),
              (override));
  MOCK_METHOD(void,
              RemoveInboundIPv4DNAT,
              (AutoDNATTarget auto_dnat_target,
               const ShillClient::Device& shill_device,
               const net_base::IPv4Address& ipv4_addr),
              (override));
  MOCK_METHOD(void, EnableQoSDetection, (), (override));
  MOCK_METHOD(void, DisableQoSDetection, (), (override));
  MOCK_METHOD(void, EnableQoSApplyingDSCP, (std::string_view), (override));
  MOCK_METHOD(void, DisableQoSApplyingDSCP, (std::string_view), (override));
  MOCK_METHOD(void,
              UpdateDoHProvidersForQoS,
              (IpFamily, const std::vector<net_base::IPAddress>&),
              (override));
  MOCK_METHOD(void, AddBorealisQoSRule, (std::string_view), (override));
  MOCK_METHOD(void, RemoveBorealisQoSRule, (std::string_view), (override));
  bool AddAdbPortAccessRule(std::string_view ifname) override { return true; }
  void DeleteAdbPortAccessRule(std::string_view ifname) override {}
  MOCK_METHOD(bool,
              CheckChain,
              (IpFamily family, Iptables::Table table, std::string_view chain),
              (override));
  MOCK_METHOD(bool,
              AddChain,
              (IpFamily family, Iptables::Table table, std::string_view chain),
              (override));
  MOCK_METHOD(bool,
              RemoveChain,
              (IpFamily family, Iptables::Table table, std::string_view chain),
              (override));
  MOCK_METHOD(bool,
              FlushChain,
              (IpFamily family, Iptables::Table table, std::string_view chain),
              (override));
  MOCK_METHOD(bool,
              ModifyChain,
              (IpFamily family,
               Iptables::Table table,
               Iptables::Command command,
               std::string_view chain,
               bool log_failures),
              (override));
  MOCK_METHOD(bool,
              ModifyClatAcceptRules,
              (Iptables::Command command, std::string_view ifname),
              (override));
  MOCK_METHOD(bool,
              ModifyIptables,
              (IpFamily family,
               Iptables::Table table,
               Iptables::Command command,
               std::string_view chain,
               const std::vector<std::string_view>& argv,
               bool log_failures),
              (override));
  MOCK_METHOD(bool,
              AddIPv6NeighborProxy,
              (std::string_view ifname, const net_base::IPv6Address& ipv6_addr),
              (override));
  MOCK_METHOD(void,
              RemoveIPv6NeighborProxy,
              (std::string_view ifname, const net_base::IPv6Address& ipv6_addr),
              (override));
  MOCK_METHOD(bool,
              AddIPv6HostRoute,
              (std::string_view ifname,
               const net_base::IPv6CIDR& ipv6_cidr,
               const std::optional<net_base::IPv6Address>& src_addr),
              (override));
  MOCK_METHOD(void,
              RemoveIPv6HostRoute,
              (const net_base::IPv6CIDR& ipv6_cidr),
              (override));
  MOCK_METHOD(void,
              StartConnectionPinning,
              (const ShillClient::Device&),
              (override));
  MOCK_METHOD(void,
              StopConnectionPinning,
              (const ShillClient::Device&),
              (override));
  MOCK_METHOD(void,
              StartSourceIPv6PrefixEnforcement,
              (const ShillClient::Device&),
              (override));
  MOCK_METHOD(void,
              StopSourceIPv6PrefixEnforcement,
              (const ShillClient::Device&),
              (override));
  MOCK_METHOD(void,
              UpdateSourceEnforcementIPv6Prefix,
              (const ShillClient::Device&,
               const std::optional<net_base::IPv6CIDR>&),
              (override));
  MOCK_METHOD(bool,
              StartDownstreamNetwork,
              (const DownstreamNetworkInfo&),
              (override));
  MOCK_METHOD(void,
              StopDownstreamNetwork,
              (const DownstreamNetworkInfo&),
              (override));
};

}  // namespace patchpanel

#endif  // PATCHPANEL_MOCK_DATAPATH_H_
