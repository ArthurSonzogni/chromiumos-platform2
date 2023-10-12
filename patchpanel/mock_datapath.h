// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_MOCK_DATAPATH_H_
#define PATCHPANEL_MOCK_DATAPATH_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <gmock/gmock.h>

#include "patchpanel/datapath.h"
#include "patchpanel/iptables.h"
#include "patchpanel/shill_client.h"

namespace patchpanel {

// ARC networking data path configuration utility.
class MockDatapath : public Datapath {
 public:
  MockDatapath();
  MockDatapath(const MockDatapath&) = delete;
  MockDatapath& operator=(const MockDatapath&) = delete;

  ~MockDatapath();

  MOCK_METHOD(void, Start, (), (override));
  MOCK_METHOD(void, Stop, (), (override));
  MOCK_METHOD(bool,
              NetnsAttachName,
              (const std::string& netns_name, pid_t netns_pid),
              (override));
  MOCK_METHOD(bool,
              NetnsDeleteName,
              (const std::string& netns_name),
              (override));

  MOCK_METHOD(bool,
              AddBridge,
              (const std::string& ifname, const net_base::IPv4CIDR& cidr),
              (override));
  MOCK_METHOD(void, RemoveBridge, (const std::string& ifname), (override));
  MOCK_METHOD(bool,
              AddToBridge,
              (const std::string& br_ifname, const std::string& ifname),
              (override));

  MOCK_METHOD(std::string,
              AddTunTap,
              (const std::string& name,
               const std::optional<MacAddress>& mac_addr,
               const std::optional<net_base::IPv4CIDR>& ipv4_cidr,
               const std::string& user,
               DeviceMode dev_mode),
              (override));
  MOCK_METHOD(void,
              RemoveTunTap,
              (const std::string& ifname, DeviceMode dev_mode),
              (override));
  MOCK_METHOD(bool,
              ConnectVethPair,
              (pid_t pid,
               const std::string& netns_name,
               const std::string& veth_ifname,
               const std::string& peer_ifname,
               const MacAddress& remote_mac_addr,
               const net_base::IPv4CIDR& remote_ipv4_cidr,
               const std::optional<net_base::IPv6CIDR>& remote_ipv6_cidr,
               bool remote_multicast_flag),
              (override));
  MOCK_METHOD(void, RemoveInterface, (const std::string& ifname), (override));
  MOCK_METHOD(void,
              StartRoutingDevice,
              (const ShillClient::Device& shill_device,
               const std::string& int_ifname,
               TrafficSource source,
               bool static_ipv6),
              (override));
  MOCK_METHOD(void,
              StartRoutingDeviceAsSystem,
              (const std::string& int_ifname,
               TrafficSource source,
               bool static_ipv6),
              (override));
  MOCK_METHOD(void,
              StartRoutingDeviceAsUser,
              (const std::string& int_ifname,
               TrafficSource source,
               const net_base::IPv4Address& int_ipv4_addr,
               std::optional<net_base::IPv4Address> peer_ipv4_addr,
               std::optional<net_base::IPv6Address> int_ipv6_addr,
               std::optional<net_base::IPv6Address> peer_ipv6_addr),
              (override));
  MOCK_METHOD(void,
              StopRoutingDevice,
              (const std::string& int_ifname, TrafficSource source),
              (override));
  MOCK_METHOD(bool,
              MaskInterfaceFlags,
              (const std::string& ifname, uint16_t on, uint16_t off),
              (override));
  MOCK_METHOD(bool,
              AddIPv4RouteToTable,
              (const std::string& ifname,
               const net_base::IPv4CIDR& ipv4_cidr,
               int table_id),
              (override));
  MOCK_METHOD(void,
              DeleteIPv4RouteFromTable,
              (const std::string& ifname,
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
              (const std::string& ifname, const bool enable),
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
  MOCK_METHOD(bool,
              AddAdbPortAccessRule,
              (const std::string& ifname),
              (override));
  MOCK_METHOD(void,
              DeleteAdbPortAccessRule,
              (const std::string& ifname),
              (override));
  MOCK_METHOD(bool,
              CheckChain,
              (IpFamily family,
               Iptables::Table table,
               const std::string& chain),
              (override));
  MOCK_METHOD(bool,
              AddChain,
              (IpFamily family,
               Iptables::Table table,
               const std::string& chain),
              (override));
  MOCK_METHOD(bool,
              RemoveChain,
              (IpFamily family,
               Iptables::Table table,
               const std::string& chain),
              (override));
  MOCK_METHOD(bool,
              FlushChain,
              (IpFamily family,
               Iptables::Table table,
               const std::string& chain),
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
              (Iptables::Command command, const std::string& ifname),
              (override));
  MOCK_METHOD(bool,
              ModifyIptables,
              (IpFamily family,
               Iptables::Table table,
               Iptables::Command command,
               std::string_view chain,
               const std::vector<std::string>& argv,
               bool log_failures,
               std::optional<base::TimeDelta> timeout),
              (override));
  MOCK_METHOD(bool,
              AddIPv6NeighborProxy,
              (const std::string& ifname,
               const net_base::IPv6Address& ipv6_addr),
              (override));
  MOCK_METHOD(void,
              RemoveIPv6NeighborProxy,
              (const std::string& ifname,
               const net_base::IPv6Address& ipv6_addr),
              (override));
  MOCK_METHOD(bool,
              AddIPv6HostRoute,
              (const std::string& ifname,
               const net_base::IPv6CIDR& ipv6_cidr,
               const std::optional<net_base::IPv6Address>& src_addr),
              (override));
  MOCK_METHOD(void,
              RemoveIPv6HostRoute,
              (const net_base::IPv6CIDR& ipv6_cidr),
              (override));
};

}  // namespace patchpanel

#endif  // PATCHPANEL_MOCK_DATAPATH_H_
