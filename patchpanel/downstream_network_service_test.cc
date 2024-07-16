// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/downstream_network_service.h"

#include <sys/socket.h>

#include <map>
#include <memory>
#include <optional>

#include <base/files/scoped_file.h>
#include <base/functional/callback.h>
#include <chromeos/net-base/ipv4_address.h>
#include <chromeos/net-base/ipv6_address.h>
#include <chromeos/net-base/mac_address.h>
#include <dbus/object_path.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <metrics/metrics_library_mock.h>
#include <patchpanel/proto_bindings/patchpanel_service.pb.h>

#include "patchpanel/fake_process_runner.h"
#include "patchpanel/fake_shill_client.h"
#include "patchpanel/fake_system.h"
#include "patchpanel/mock_conntrack_monitor.h"
#include "patchpanel/mock_counters_service.h"
#include "patchpanel/mock_datapath.h"
#include "patchpanel/mock_forwarding_service.h"
#include "patchpanel/mock_guest_ipv6_service.h"
#include "patchpanel/mock_lifeline_fd_service.h"
#include "patchpanel/mock_routing_service.h"
#include "patchpanel/mock_rtnl_client.h"
#include "patchpanel/noop_subprocess_controller.h"

using testing::_;
using testing::Mock;
using testing::Return;
using testing::WithArgs;

namespace patchpanel {

MATCHER_P(ShillDeviceHasInterfaceName, expected_ifname, "") {
  return arg.ifname == expected_ifname;
}

MATCHER_P(EqShillDevice, expected_shill_device, "") {
  return arg.technology == expected_shill_device.technology &&
         arg.ifindex == expected_shill_device.ifindex &&
         arg.ifname == expected_shill_device.ifname &&
         arg.ipconfig.ipv6_cidr == expected_shill_device.ipconfig.ipv6_cidr;
}

MATCHER_P(DownstreamNetworkInfoHasInterfaceName,
          expected_downstream_ifname,
          "") {
  return arg.downstream_ifname == expected_downstream_ifname;
}

base::ScopedFD MakeTestSocket() {
  return base::ScopedFD(socket(AF_INET, SOCK_DGRAM, 0));
}

class DownstreamNetworkServiceTest : public testing::Test {
 protected:
  DownstreamNetworkServiceTest()
      : datapath_(&process_runner_, &system_),
        shill_client_(shill_client_helper_.FakeClient()),
        ipv6_svc_(&nd_proxy_),
        counters_svc_(&datapath_, &conntrack_monitor_),
        downstream_network_svc_(&metrics_,
                                &system_,
                                &datapath_,
                                &routing_svc_,
                                &forwarding_svc_,
                                &rtnl_client_,
                                &lifeline_fd_svc_,
                                shill_client_.get(),
                                &ipv6_svc_,
                                &counters_svc_) {}

  MetricsLibraryMock metrics_;
  FakeProcessRunner process_runner_;
  FakeSystem system_;
  MockDatapath datapath_;
  MockRoutingService routing_svc_;
  MockForwardingService forwarding_svc_;
  MockRTNLClient rtnl_client_;
  MockLifelineFDService lifeline_fd_svc_;
  FakeShillClientHelper shill_client_helper_;
  std::unique_ptr<FakeShillClient> shill_client_;
  NoopSubprocessController nd_proxy_;
  MockGuestIPv6Service ipv6_svc_;
  MockConntrackMonitor conntrack_monitor_;
  MockCountersService counters_svc_;
  DownstreamNetworkService downstream_network_svc_;
};

TEST_F(DownstreamNetworkServiceTest,
       CreateTetheredNetworkWithUpstreamShillDevice) {
  ShillClient::Device upstream;
  upstream.technology = net_base::Technology::kCellular;
  upstream.ifindex = 1;
  upstream.ifname = "qmapmux9";
  upstream.service_path = "/service/1";
  shill_client_->SetFakeDeviceProperties(dbus::ObjectPath("/device/rmnet_ipa0"),
                                         upstream);
  base::OnceClosure on_lifeline_fd_event;
  EXPECT_CALL(lifeline_fd_svc_, AddLifelineFD)
      .WillOnce(WithArgs<1>([&](base::OnceClosure cb) {
        on_lifeline_fd_event = std::move(cb);
        return base::ScopedClosureRunner(base::DoNothing());
      }));
  EXPECT_CALL(datapath_, StartDownstreamNetwork(
                             DownstreamNetworkInfoHasInterfaceName("ap0")))
      .WillOnce(Return(true));
  EXPECT_CALL(forwarding_svc_,
              StartIPv6NDPForwarding(EqShillDevice(upstream), "ap0", _, _));
  EXPECT_CALL(system_, SysNetGet(System::SysNet::kIPv6HopLimit, "qmapmux9"))
      .WillOnce(Return("64"));
  EXPECT_CALL(routing_svc_, AllocateNetworkID).WillOnce(Return(455));
  EXPECT_CALL(routing_svc_, AssignInterfaceToNetwork(455, "ap0", _))
      .WillOnce(Return(true));

  // When a shill Device exists for the upstream, none of the extra setup of
  // DownstreamNetworkService::StartTetheringUpstreamNetwork should be started.
  EXPECT_CALL(counters_svc_, OnPhysicalDeviceAdded).Times(0);
  EXPECT_CALL(datapath_, StartConnectionPinning).Times(0);
  EXPECT_CALL(datapath_, StartSourceIPv6PrefixEnforcement).Times(0);
  EXPECT_CALL(datapath_, UpdateSourceEnforcementIPv6Prefix).Times(0);
  EXPECT_CALL(ipv6_svc_, OnUplinkIPv6Changed).Times(0);
  EXPECT_CALL(ipv6_svc_, UpdateUplinkIPv6DNS).Times(0);

  TetheredNetworkRequest request;
  request.set_upstream_ifname("qmapmux9");
  request.set_ifname("ap0");
  request.set_enable_ipv6(true);
  // TODO(b/336883268): Add coverage for IPv4 DHCP server logic.
  base::ScopedFD lifeline_fd = MakeTestSocket();
  patchpanel::TetheredNetworkResponse response =
      downstream_network_svc_.CreateTetheredNetwork(request,
                                                    std::move(lifeline_fd));
  EXPECT_EQ(DownstreamNetworkResult::SUCCESS, response.response_code());
  EXPECT_EQ("ap0", response.downstream_network().downstream_ifname());
  EXPECT_EQ(455, response.downstream_network().network_id());

  Mock::VerifyAndClearExpectations(&system_);
  Mock::VerifyAndClearExpectations(&datapath_);
  Mock::VerifyAndClearExpectations(&forwarding_svc_);
  Mock::VerifyAndClearExpectations(&lifeline_fd_svc_);
  Mock::VerifyAndClearExpectations(&ipv6_svc_);
  Mock::VerifyAndClearExpectations(&counters_svc_);
  Mock::VerifyAndClearExpectations(&routing_svc_);

  EXPECT_CALL(datapath_, StopDownstreamNetwork(
                             DownstreamNetworkInfoHasInterfaceName("ap0")));
  EXPECT_CALL(forwarding_svc_,
              StopIPv6NDPForwarding(EqShillDevice(upstream), "ap0"));
  EXPECT_CALL(routing_svc_, ForgetNetworkID(455));

  // When a shill Device existed for the upstream, none of the extra teardown of
  // DownstreamNetworkService::StopTetheringUpstreamNetwork should be triggered.
  EXPECT_CALL(counters_svc_, OnPhysicalDeviceRemoved).Times(0);
  EXPECT_CALL(datapath_, StopConnectionPinning).Times(0);
  EXPECT_CALL(datapath_, StopSourceIPv6PrefixEnforcement).Times(0);
  EXPECT_CALL(ipv6_svc_, StopUplink).Times(0);
  EXPECT_CALL(ipv6_svc_, OnUplinkIPv6Changed).Times(0);

  // Trigger DownstreamNetworkService::OnDownstreamNetworkAutoclose()
  std::move(on_lifeline_fd_event).Run();
}

TEST_F(DownstreamNetworkServiceTest,
       CreateTetheredNetworkWithoutUpstreamShillDevice) {
  net_base::IPv6Address ipv6_addr(
      net_base::IPv6Address(0x20, 0x01, 0xdb, 0x80, 0x00, 0x00, 0x00, 0x00,
                            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xab, 0x12));
  net_base::IPv6Address ipv6_dns(
      net_base::IPv6Address(0x20, 0x01, 0xdb, 0x80, 0x00, 0x00, 0x00, 0x00,
                            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x88, 0x88));
  TetheredNetworkRequest request;
  request.set_upstream_ifname("qmapmux9");
  request.set_ifname("ap0");
  request.set_enable_ipv6(true);
  UplinkIPv6Configuration* ipv6_config = request.mutable_uplink_ipv6_config();
  IPAddrCIDR* ipv6_prefix = ipv6_config->mutable_uplink_ipv6_cidr();
  ipv6_prefix->set_addr(ipv6_addr.ToByteString());
  ipv6_prefix->set_prefix_len(64);
  ipv6_config->add_dns_servers(ipv6_dns.ToByteString());
  // TODO(b/336883268): Add coverage for IPv4 DHCP server logic.

  // This mimics the primary Cellular Network
  ShillClient::Device primary_cellular_network;
  primary_cellular_network.technology = net_base::Technology::kCellular;
  primary_cellular_network.ifindex = 1;
  primary_cellular_network.ifname = "qmapmux1";
  primary_cellular_network.service_path = "/service/1";
  primary_cellular_network.shill_device_interface_property = "rmnet_ipa0";
  shill_client_->SetFakeDeviceProperties(dbus::ObjectPath("/device/rmnet_ipa0"),
                                         primary_cellular_network);

  ShillClient::Device expected_upstream_device;
  expected_upstream_device.technology = net_base::Technology::kCellular;
  expected_upstream_device.ifindex = 2;
  expected_upstream_device.ifname = "qmapmux9";
  expected_upstream_device.ipconfig.ipv6_cidr =
      net_base::IPv6CIDR::CreateFromAddressAndPrefix(ipv6_addr, 64);

  base::OnceClosure on_lifeline_fd_event;
  EXPECT_CALL(lifeline_fd_svc_, AddLifelineFD)
      .WillOnce(WithArgs<1>([&](base::OnceClosure cb) {
        on_lifeline_fd_event = std::move(cb);
        return base::ScopedClosureRunner(base::DoNothing());
      }));
  EXPECT_CALL(datapath_, StartDownstreamNetwork(
                             DownstreamNetworkInfoHasInterfaceName("ap0")))
      .WillOnce(Return(true));
  EXPECT_CALL(forwarding_svc_,
              StartIPv6NDPForwarding(EqShillDevice(expected_upstream_device),
                                     "ap0", _, _));
  EXPECT_CALL(system_, SysNetGet(System::SysNet::kIPv6HopLimit, "qmapmux9"))
      .WillOnce(Return("64"));
  ON_CALL(system_, IfNametoindex("qmapmux9")).WillByDefault(Return(2));
  EXPECT_CALL(routing_svc_, AllocateNetworkID).WillOnce(Return(291));
  EXPECT_CALL(routing_svc_, AssignInterfaceToNetwork(291, "ap0", _))
      .WillOnce(Return(true));

  // When a shill Device does not exists for the upstream, expect extra setup of
  // DownstreamNetworkService::StartTetheringUpstreamNetwork.
  EXPECT_CALL(counters_svc_, OnPhysicalDeviceAdded("qmapmux9"));
  EXPECT_CALL(datapath_,
              StartConnectionPinning(EqShillDevice(expected_upstream_device)));
  EXPECT_CALL(datapath_, StartSourceIPv6PrefixEnforcement(
                             EqShillDevice(expected_upstream_device)));
  EXPECT_CALL(datapath_, UpdateSourceEnforcementIPv6Prefix(
                             EqShillDevice(expected_upstream_device), _));
  EXPECT_CALL(ipv6_svc_,
              OnUplinkIPv6Changed(EqShillDevice(expected_upstream_device)));
  EXPECT_CALL(ipv6_svc_,
              UpdateUplinkIPv6DNS(EqShillDevice(expected_upstream_device)));

  base::ScopedFD lifeline_fd = MakeTestSocket();
  patchpanel::TetheredNetworkResponse response =
      downstream_network_svc_.CreateTetheredNetwork(request,
                                                    std::move(lifeline_fd));
  EXPECT_EQ(DownstreamNetworkResult::SUCCESS, response.response_code());
  EXPECT_EQ("ap0", response.downstream_network().downstream_ifname());
  EXPECT_EQ(291, response.downstream_network().network_id());

  Mock::VerifyAndClearExpectations(&system_);
  Mock::VerifyAndClearExpectations(&datapath_);
  Mock::VerifyAndClearExpectations(&forwarding_svc_);
  Mock::VerifyAndClearExpectations(&lifeline_fd_svc_);
  Mock::VerifyAndClearExpectations(&ipv6_svc_);
  Mock::VerifyAndClearExpectations(&counters_svc_);
  Mock::VerifyAndClearExpectations(&routing_svc_);

  EXPECT_CALL(datapath_, StopDownstreamNetwork(
                             DownstreamNetworkInfoHasInterfaceName("ap0")));
  EXPECT_CALL(
      forwarding_svc_,
      StopIPv6NDPForwarding(EqShillDevice(expected_upstream_device), "ap0"));
  EXPECT_CALL(routing_svc_, ForgetNetworkID(291));
  // When a shill Device did not exists for the upstream, expect extra teardown
  // of DownstreamNetworkService::StopTetheringUpstreamNetwork.
  EXPECT_CALL(datapath_,
              StopConnectionPinning(EqShillDevice(expected_upstream_device)));
  EXPECT_CALL(datapath_, StopSourceIPv6PrefixEnforcement(
                             EqShillDevice(expected_upstream_device)));
  EXPECT_CALL(ipv6_svc_, StopUplink(EqShillDevice(expected_upstream_device)));
  EXPECT_CALL(counters_svc_, OnPhysicalDeviceRemoved("qmapmux9"));

  // Trigger DownstreamNetworkService::OnDownstreamNetworkAutoclose()
  std::move(on_lifeline_fd_event).Run();
}

TEST_F(DownstreamNetworkServiceTest, GetDownstreamNetworkInfo) {
  ShillClient::Device upstream;
  upstream.technology = net_base::Technology::kCellular;
  upstream.ifindex = 1;
  upstream.ifname = "qmapmux9";
  upstream.service_path = "/service/1";
  shill_client_->SetFakeDeviceProperties(dbus::ObjectPath("/device/rmnet_ipa0"),
                                         upstream);
  EXPECT_CALL(lifeline_fd_svc_, AddLifelineFD)
      .WillOnce(Return(base::ScopedClosureRunner(base::DoNothing())));
  EXPECT_CALL(datapath_, StartDownstreamNetwork(
                             DownstreamNetworkInfoHasInterfaceName("ap0")))
      .WillOnce(Return(true));
  EXPECT_CALL(forwarding_svc_,
              StartIPv6NDPForwarding(EqShillDevice(upstream), "ap0", _, _));
  EXPECT_CALL(system_, SysNetGet(System::SysNet::kIPv6HopLimit, "qmapmux9"))
      .WillOnce(Return("64"));
  EXPECT_CALL(routing_svc_, AllocateNetworkID).WillOnce(Return(83));
  EXPECT_CALL(routing_svc_, AssignInterfaceToNetwork(83, "ap0", _))
      .WillOnce(Return(true));

  TetheredNetworkRequest request;
  request.set_upstream_ifname("qmapmux9");
  request.set_ifname("ap0");
  request.set_enable_ipv6(true);
  base::ScopedFD lifeline_fd = MakeTestSocket();
  patchpanel::TetheredNetworkResponse response =
      downstream_network_svc_.CreateTetheredNetwork(request,
                                                    std::move(lifeline_fd));
  ASSERT_EQ(DownstreamNetworkResult::SUCCESS, response.response_code());
  Mock::VerifyAndClearExpectations(&system_);
  Mock::VerifyAndClearExpectations(&datapath_);
  Mock::VerifyAndClearExpectations(&forwarding_svc_);
  Mock::VerifyAndClearExpectations(&lifeline_fd_svc_);

  net_base::IPv6Address ipv6_neighbor(0x20, 0x01, 0x48, 0x60, 0x48, 0x60, 0x00,
                                      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                      0x88, 0x88);
  net_base::IPv4Address ipv4_neighbor(192, 168, 3, 18);
  net_base::MacAddress neighbor_mac(0x01, 0x23, 0x45, 0x67, 0x89, 0xab);
  std::map<net_base::IPv4Address, net_base::MacAddress> ipv4_neighbors;
  std::map<net_base::IPv6Address, net_base::MacAddress> ipv6_neighbors;
  ipv4_neighbors.emplace(ipv4_neighbor, neighbor_mac);
  ipv6_neighbors.emplace(ipv6_neighbor, neighbor_mac);
  ON_CALL(rtnl_client_, GetIPv4NeighborMacTable)
      .WillByDefault(Return(ipv4_neighbors));
  ON_CALL(rtnl_client_, GetIPv6NeighborMacTable)
      .WillByDefault(Return(ipv6_neighbors));
  ON_CALL(system_, IfNametoindex("ap0")).WillByDefault(Return(1));

  GetDownstreamNetworkInfoResponse network_info =
      downstream_network_svc_.GetDownstreamNetworkInfo("ap0");
  EXPECT_TRUE(network_info.success());
  EXPECT_EQ("ap0", network_info.downstream_network().downstream_ifname());
  EXPECT_EQ(83, network_info.downstream_network().network_id());
  EXPECT_EQ(1, network_info.clients_info().size());
  const NetworkClientInfo& client_info = network_info.clients_info(0);
  EXPECT_EQ(neighbor_mac,
            net_base::MacAddress::CreateFromBytes(client_info.mac_addr()));
  EXPECT_EQ(ipv4_neighbor,
            net_base::IPv4Address::CreateFromBytes(client_info.ipv4_addr()));
  EXPECT_EQ(1, client_info.ipv6_addresses().size());
  EXPECT_EQ(ipv6_neighbor, net_base::IPv6Address::CreateFromBytes(
                               client_info.ipv6_addresses(0)));
}

TEST_F(DownstreamNetworkServiceTest, CalculateDownstreamCurHopLimit) {
  FakeSystem system;

  // Successful case.
  EXPECT_CALL(system, SysNetGet(System::SysNet::kIPv6HopLimit, "wwan0"))
      .WillOnce(Return("64"));
  EXPECT_EQ(DownstreamNetworkService::CalculateDownstreamCurHopLimit(
                reinterpret_cast<System*>(&system), "wwan0"),
            63);

  // Failure case.
  EXPECT_CALL(system, SysNetGet(System::SysNet::kIPv6HopLimit, "wwan1"))
      .WillOnce(Return(""));
  EXPECT_EQ(DownstreamNetworkService::CalculateDownstreamCurHopLimit(
                reinterpret_cast<System*>(&system), "wwan1"),
            std::nullopt);
}
}  // namespace patchpanel
