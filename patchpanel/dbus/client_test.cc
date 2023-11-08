// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/dbus/client.h"

#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <dbus/mock_bus.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <patchpanel/proto_bindings/patchpanel_service.pb.h>

#include "patchpanel/dbus/mock_patchpanel_proxy.h"

namespace patchpanel {
namespace {

using ::testing::_;
using ::testing::AllOf;
using ::testing::DoAll;
using ::testing::Property;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::SetArgPointee;

class ClientTest : public testing::Test {
 protected:
  void SetUp() override {
    dbus_ = new dbus::MockBus{dbus::Bus::Options{}};
    proxy_ = new MockPatchPanelProxy();
    client_ = Client::NewForTesting(
        dbus_,
        std::unique_ptr<org::chromium::PatchPanelProxyInterface>(proxy_));
  }

  scoped_refptr<dbus::MockBus> dbus_;
  std::unique_ptr<Client> client_;
  MockPatchPanelProxy* proxy_;  // It's owned by |client_|.
};

TEST_F(ClientTest, NotifyArcStartup) {
  const pid_t pid = 3456;
  EXPECT_CALL(*proxy_,
              ArcStartup(Property(&ArcStartupRequest::pid, pid), _, _, _))
      .WillOnce(Return(true));

  const bool result = client_->NotifyArcStartup(pid);
  EXPECT_TRUE(result);
}

TEST_F(ClientTest, NotifyArcShutdown) {
  EXPECT_CALL(*proxy_, ArcShutdown(_, _, _, _)).WillOnce(Return(true));

  const bool result = client_->NotifyArcShutdown();
  EXPECT_TRUE(result);
}

TEST_F(ClientTest, NotifyArcVmStartup) {
  const uint32_t cid = 5;
  const auto arc0_addr =
      *net_base::IPv4Address::CreateFromString("100.115.92.2");
  ArcVmStartupResponse response_proto;
  response_proto.add_tap_device_ifnames("vmtap0");
  response_proto.add_tap_device_ifnames("vmtap1");
  response_proto.add_tap_device_ifnames("vmtap2");
  response_proto.set_arc0_ipv4_address(arc0_addr.ToByteString());

  EXPECT_CALL(*proxy_,
              ArcVmStartup(Property(&ArcVmStartupRequest::cid, cid), _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(response_proto), Return(true)));

  const auto arcvm_alloc = client_->NotifyArcVmStartup(cid);
  ASSERT_TRUE(arcvm_alloc.has_value());
  EXPECT_EQ(arcvm_alloc->tap_device_ifnames,
            std::vector<std::string>({"vmtap0", "vmtap1", "vmtap2"}));
  EXPECT_EQ(arcvm_alloc->arc0_ipv4_address, arc0_addr);
}

TEST_F(ClientTest, NotifyArcVmShutdown) {
  const uint32_t cid = 5;

  EXPECT_CALL(*proxy_,
              ArcVmShutdown(Property(&ArcVmShutdownRequest::cid, cid), _, _, _))
      .WillOnce(Return(true));

  const bool result = client_->NotifyArcVmShutdown(cid);
  EXPECT_TRUE(result);
}

TEST_F(ClientTest, NotifyTerminaVmStartup) {
  const uint32_t cid = 5;
  const auto termina_ipv4_subnet =
      *net_base::IPv4CIDR::CreateFromCIDRString("100.115.92.24/30");
  const auto termina_ipv4_address =
      *net_base::IPv4Address::CreateFromString("100.115.92.26");
  const auto gateway_ipv4_address =
      *net_base::IPv4Address::CreateFromString("100.115.92.25");
  const auto container_ipv4_subnet =
      *net_base::IPv4CIDR::CreateFromCIDRString("100.115.92.192/28");
  const auto container_ipv4_address =
      *net_base::IPv4Address::CreateFromString("100.115.92.193");

  TerminaVmStartupResponse response_proto;
  response_proto.set_tap_device_ifname("vmtap1");
  auto* response_subnet = response_proto.mutable_ipv4_subnet();
  response_subnet->set_addr(termina_ipv4_subnet.address().ToByteString());
  response_subnet->set_prefix_len(
      static_cast<uint32_t>(termina_ipv4_subnet.prefix_length()));
  response_proto.set_ipv4_address(termina_ipv4_address.ToByteString());
  response_proto.set_gateway_ipv4_address(gateway_ipv4_address.ToByteString());
  auto* response_container_subnet =
      response_proto.mutable_container_ipv4_subnet();
  response_container_subnet->set_addr(
      container_ipv4_subnet.address().ToByteString());
  response_container_subnet->set_prefix_len(
      static_cast<uint32_t>(container_ipv4_subnet.prefix_length()));
  response_proto.set_container_ipv4_address(
      container_ipv4_address.ToByteString());

  EXPECT_CALL(
      *proxy_,
      TerminaVmStartup(Property(&TerminaVmStartupRequest::cid, cid), _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(response_proto), Return(true)));

  const auto result = client_->NotifyTerminaVmStartup(cid);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ("vmtap1", result->tap_device_ifname);
  EXPECT_EQ(termina_ipv4_subnet, result->termina_ipv4_subnet);
  EXPECT_EQ(termina_ipv4_address, result->termina_ipv4_address);
  EXPECT_EQ(gateway_ipv4_address, result->gateway_ipv4_address);
  EXPECT_EQ(container_ipv4_subnet, result->container_ipv4_subnet);
  EXPECT_EQ(container_ipv4_address, result->container_ipv4_address);
}

TEST_F(ClientTest, NotifyTerminaVmShutdown) {
  const uint32_t cid = 5;

  EXPECT_CALL(
      *proxy_,
      TerminaVmShutdown(Property(&TerminaVmShutdownRequest::cid, cid), _, _, _))
      .WillOnce(Return(true));

  bool result = client_->NotifyTerminaVmShutdown(cid);
  EXPECT_TRUE(result);
}

TEST_F(ClientTest, NotifyParallelsVmStartup) {
  const uint64_t id = 5;
  const int subnet_index = 4;
  const auto parallels_ipv4_subnet =
      *net_base::IPv4CIDR::CreateFromCIDRString("100.115.93.0/29");
  const auto parallels_ipv4_address =
      *net_base::IPv4Address::CreateFromString("100.115.93.2");

  ParallelsVmStartupResponse response_proto;
  response_proto.set_tap_device_ifname("vmtap2");
  auto* response_subnet = response_proto.mutable_ipv4_subnet();
  response_subnet->set_addr(parallels_ipv4_subnet.address().ToByteString());
  response_subnet->set_prefix_len(
      static_cast<uint32_t>(parallels_ipv4_subnet.prefix_length()));
  response_proto.set_ipv4_address(parallels_ipv4_address.ToByteString());

  EXPECT_CALL(*proxy_,
              ParallelsVmStartup(
                  AllOf(Property(&ParallelsVmStartupRequest::id, id),
                        Property(&ParallelsVmStartupRequest::subnet_index,
                                 subnet_index)),
                  _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(response_proto), Return(true)));

  auto result = client_->NotifyParallelsVmStartup(id, subnet_index);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ("vmtap2", result->tap_device_ifname);
  EXPECT_EQ(parallels_ipv4_subnet, result->parallels_ipv4_subnet);
  EXPECT_EQ(parallels_ipv4_address, result->parallels_ipv4_address);
}

TEST_F(ClientTest, NotifyParallelsVmShutdown) {
  const uint64_t id = 5;

  EXPECT_CALL(*proxy_,
              ParallelsVmShutdown(Property(&ParallelsVmShutdownRequest::id, id),
                                  _, _, _))
      .WillOnce(Return(true));

  const bool result = client_->NotifyParallelsVmShutdown(id);
  EXPECT_TRUE(result);
}

TEST_F(ClientTest, NotifyBruschettaVmStartup) {
  const uint64_t id = 5;
  const std::string tap_ifname = "vmtap2";
  const auto bruschetta_ipv4_subnet =
      *net_base::IPv4CIDR::CreateFromCIDRString("100.115.93.0/29");
  const auto bruschetta_ipv4_address =
      *net_base::IPv4Address::CreateFromString("100.115.93.2");
  const auto gateway_ipv4_address =
      *net_base::IPv4Address::CreateFromString("100.115.93.1");

  BruschettaVmStartupResponse response_proto;
  response_proto.set_tap_device_ifname(tap_ifname);
  auto* response_subnet = response_proto.mutable_ipv4_subnet();
  response_subnet->set_addr(bruschetta_ipv4_subnet.address().ToByteString());
  response_subnet->set_prefix_len(
      static_cast<uint32_t>(bruschetta_ipv4_subnet.prefix_length()));
  response_proto.set_ipv4_address(bruschetta_ipv4_address.ToByteString());
  response_proto.set_gateway_ipv4_address(gateway_ipv4_address.ToByteString());

  EXPECT_CALL(*proxy_,
              BruschettaVmStartup(Property(&BruschettaVmStartupRequest::id, id),
                                  _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(response_proto), Return(true)));

  auto result = client_->NotifyBruschettaVmStartup(id);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->tap_device_ifname, tap_ifname);
  EXPECT_EQ(result->bruschetta_ipv4_subnet, bruschetta_ipv4_subnet);
  EXPECT_EQ(result->bruschetta_ipv4_address, bruschetta_ipv4_address);
  EXPECT_EQ(result->gateway_ipv4_address, gateway_ipv4_address);
}

TEST_F(ClientTest, NotifyBruschettaVmShutdown) {
  const uint64_t id = 5;

  EXPECT_CALL(*proxy_,
              BruschettaVmShutdown(
                  Property(&BruschettaVmShutdownRequest::id, id), _, _, _))
      .WillOnce(Return(true));

  const bool result = client_->NotifyBruschettaVmShutdown(id);
  EXPECT_TRUE(result);
}

TEST_F(ClientTest, NotifyBorealisVmStartup) {
  const uint32_t id = 6;
  const std::string tap_ifname = "vmtap3";

  BorealisVmStartupResponse response_proto;
  response_proto.set_tap_device_ifname(tap_ifname);

  EXPECT_CALL(
      *proxy_,
      BorealisVmStartup(Property(&BorealisVmStartupRequest::id, id), _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(response_proto), Return(true)));

  auto result = client_->NotifyBorealisVmStartup(id);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->tap_device_ifname, tap_ifname);
}

TEST_F(ClientTest, NotifyBorealisVmShutdown) {
  const uint32_t id = 6;

  EXPECT_CALL(
      *proxy_,
      BorealisVmShutdown(Property(&BorealisVmShutdownRequest::id, id), _, _, _))
      .WillOnce(Return(true));

  const bool result = client_->NotifyBorealisVmShutdown(id);
  EXPECT_TRUE(result);
}

TEST_F(ClientTest, ConnectNamespace_Fail) {
  const pid_t invalid_pid = 3456;
  const std::string outbound_ifname = "";

  auto action = [](const patchpanel::ConnectNamespaceRequest&,
                   const base::ScopedFD&, patchpanel::ConnectNamespaceResponse*,
                   brillo::ErrorPtr* error, int) {
    *error = brillo::Error::Create(FROM_HERE, "", "", "");
    return false;
  };
  EXPECT_CALL(*proxy_, ConnectNamespace(
                           Property(&ConnectNamespaceRequest::pid, invalid_pid),
                           _, _, _, _))
      .WillOnce(action);

  const auto result =
      client_->ConnectNamespace(invalid_pid, outbound_ifname, false, true,
                                Client::TrafficSource::kSystem);
  EXPECT_FALSE(result.first.is_valid());
  EXPECT_TRUE(result.second.peer_ifname.empty());
  EXPECT_TRUE(result.second.host_ifname.empty());
  EXPECT_TRUE(result.second.peer_ipv4_address.IsZero());
  EXPECT_TRUE(result.second.host_ipv4_address.IsZero());
  EXPECT_EQ(result.second.ipv4_subnet, net_base::IPv4CIDR());
}

TEST_F(ClientTest, ConnectNamespace) {
  const pid_t pid = 3456;
  const std::string outbound_ifname = "test_ifname";
  const net_base::IPv4Address host_ipv4_addr(100, 115, 92, 129);
  const net_base::IPv4Address peer_ipv4_addr(100, 115, 92, 130);

  ConnectNamespaceResponse response_proto;
  response_proto.set_peer_ifname("veth0");
  response_proto.set_host_ifname("arc_ns0");
  response_proto.set_host_ipv4_address(host_ipv4_addr.ToInAddr().s_addr);
  response_proto.set_peer_ipv4_address(peer_ipv4_addr.ToInAddr().s_addr);
  auto* response_subnet = response_proto.mutable_ipv4_subnet();
  response_subnet->set_prefix_len(30);
  response_subnet->set_addr(std::vector<uint8_t>{100, 115, 92, 128}.data(), 4);

  EXPECT_CALL(
      *proxy_,
      ConnectNamespace(
          AllOf(Property(&ConnectNamespaceRequest::pid, pid),
                Property(&ConnectNamespaceRequest::outbound_physical_device,
                         outbound_ifname)),
          _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(response_proto), Return(true)));

  const auto result = client_->ConnectNamespace(
      pid, outbound_ifname, false, true, Client::TrafficSource::kSystem);
  EXPECT_TRUE(result.first.is_valid());
  EXPECT_EQ("arc_ns0", result.second.host_ifname);
  EXPECT_EQ("veth0", result.second.peer_ifname);
  EXPECT_EQ("100.115.92.128/30", result.second.ipv4_subnet.ToString());
  EXPECT_EQ(host_ipv4_addr, result.second.host_ipv4_address);
  EXPECT_EQ(peer_ipv4_addr, result.second.peer_ipv4_address);
}

TEST_F(ClientTest, RegisterNeighborEventHandler) {
  static Client::NeighborReachabilityEvent actual_event;
  static int call_num = 0;
  auto callback =
      base::BindRepeating([](const Client::NeighborReachabilityEvent& event) {
        call_num++;
        actual_event = event;
      });

  // Store the DBus callback.
  base::RepeatingCallback<void(
      const patchpanel::NeighborReachabilityEventSignal&)>
      registered_dbus_callback;
  EXPECT_CALL(*proxy_, RegisterNeighborReachabilityEventSignalHandler(_, _))
      .WillOnce(SaveArg<0>(&registered_dbus_callback));

  client_->RegisterNeighborReachabilityEventHandler(callback);

  // Trigger the DBus callback to simulate the signal arrival.
  NeighborReachabilityEventSignal signal_proto;
  signal_proto.set_ifindex(7);
  signal_proto.set_ip_addr("1.2.3.4");
  signal_proto.set_role(NeighborReachabilityEventSignal::GATEWAY);
  signal_proto.set_type(NeighborReachabilityEventSignal::FAILED);
  registered_dbus_callback.Run(signal_proto);

  EXPECT_EQ(call_num, 1);
  EXPECT_EQ(actual_event.ifindex, 7);
  EXPECT_EQ(actual_event.ip_addr, "1.2.3.4");
  EXPECT_EQ(actual_event.role, Client::NeighborRole::kGateway);
  EXPECT_EQ(actual_event.status, Client::NeighborStatus::kFailed);
}

TEST_F(ClientTest, RegisterNeighborEventSignal) {
  Client::NeighborReachabilityEvent event;
  event.ifindex = 1;
  event.ip_addr = "192.168.1.32";
  event.role = Client::NeighborRole::kGateway;
  event.status = Client::NeighborStatus::kFailed;

  std::stringstream stream;
  stream << event;
  EXPECT_EQ(
      "{ifindex: 1, ip_address: 192.168.1.32, role: GATEWAY, status: FAILED}",
      stream.str());
}

TEST_F(ClientTest, TrafficSourceName) {
  EXPECT_EQ("UNKNOWN",
            Client::TrafficSourceName(Client::TrafficSource::kUnknown));
  EXPECT_EQ("CHROME",
            Client::TrafficSourceName(Client::TrafficSource::kChrome));
  EXPECT_EQ("USER", Client::TrafficSourceName(Client::TrafficSource::kUser));
  EXPECT_EQ("CROSVM",
            Client::TrafficSourceName(Client::TrafficSource::kCrosVm));
  EXPECT_EQ("PARALLELS_VM",
            Client::TrafficSourceName(Client::TrafficSource::kParallelsVm));
  EXPECT_EQ("UPDATE_ENGINE",
            Client::TrafficSourceName(Client::TrafficSource::kUpdateEngine));
  EXPECT_EQ("VPN", Client::TrafficSourceName(Client::TrafficSource::kVpn));
  EXPECT_EQ("SYSTEM",
            Client::TrafficSourceName(Client::TrafficSource::kSystem));
}

TEST_F(ClientTest, ProtocolName) {
  EXPECT_EQ("UDP", Client::ProtocolName(Client::FirewallRequestProtocol::kUdp));
  EXPECT_EQ("TCP", Client::ProtocolName(Client::FirewallRequestProtocol::kTcp));
}

TEST_F(ClientTest, NeighborRoleName) {
  EXPECT_EQ("GATEWAY",
            Client::NeighborRoleName(Client::NeighborRole::kGateway));
  EXPECT_EQ("DNS_SERVER",
            Client::NeighborRoleName(Client::NeighborRole::kDnsServer));
  EXPECT_EQ(
      "GATEWAY_AND_DNS_SERVER",
      Client::NeighborRoleName(Client::NeighborRole::kGatewayAndDnsServer));
}

TEST_F(ClientTest, NeighborStatusName) {
  EXPECT_EQ("REACHABLE",
            Client::NeighborStatusName(Client::NeighborStatus::kReachable));
  EXPECT_EQ("FAILED",
            Client::NeighborStatusName(Client::NeighborStatus::kFailed));
}

TEST_F(ClientTest, SendSetFeatureFlagRequest) {
  bool enable = true;
  auto flag = patchpanel::SetFeatureFlagRequest::FeatureFlag::
      SetFeatureFlagRequest_FeatureFlag_WIFI_QOS;

  EXPECT_CALL(
      *proxy_,
      SetFeatureFlag(AllOf(Property(&SetFeatureFlagRequest::enabled, enable),
                           Property(&SetFeatureFlagRequest::flag, flag)),
                     _, _, _))
      .WillOnce(Return(true));

  const bool result =
      client_->SendSetFeatureFlagRequest(Client::FeatureFlag::kWiFiQoS, true);
  EXPECT_TRUE(result);
}

}  // namespace
}  // namespace patchpanel
