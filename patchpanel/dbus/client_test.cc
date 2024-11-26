// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/dbus/client.h"

#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <base/run_loop.h>
#include <base/test/task_environment.h>
#include <brillo/http/mock_transport.h>
#include <dbus/mock_bus.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <patchpanel/proto_bindings/patchpanel_service.pb.h>
#include <patchpanel/proto_bindings/traffic_annotation.pb.h>

#include "patchpanel/dbus/mock_patchpanel_proxy.h"
#include "patchpanel/dbus/mock_socketservice_proxy.h"
#include "socketservice/dbus-proxies.h"

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
    pp_proxy_ = new MockPatchPanelProxy();
    ss_proxy_ = new MockSocketServiceProxy();
    http_transport_ = std::make_shared<brillo::http::MockTransport>();
    client_ = Client::NewForTesting(
        dbus_,
        std::unique_ptr<org::chromium::PatchPanelProxyInterface>(pp_proxy_),
        std::unique_ptr<org::chromium::SocketServiceProxyInterface>(ss_proxy_));
  }

  scoped_refptr<dbus::MockBus> dbus_;
  std::unique_ptr<Client> client_;
  MockPatchPanelProxy* pp_proxy_;  // It's owned by |client_|.
  MockSocketServiceProxy* ss_proxy_;
  std::shared_ptr<brillo::http::MockTransport> http_transport_;
};

TEST_F(ClientTest, NotifyArcStartup) {
  const pid_t pid = 3456;
  EXPECT_CALL(*pp_proxy_,
              ArcStartup(Property(&ArcStartupRequest::pid, pid), _, _, _))
      .WillOnce(Return(true));

  const bool result = client_->NotifyArcStartup(pid);
  EXPECT_TRUE(result);
}

TEST_F(ClientTest, NotifyArcShutdown) {
  EXPECT_CALL(*pp_proxy_, ArcShutdown(_, _, _, _)).WillOnce(Return(true));

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

  EXPECT_CALL(*pp_proxy_,
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

  EXPECT_CALL(*pp_proxy_,
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
      *pp_proxy_,
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
      *pp_proxy_,
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

  EXPECT_CALL(*pp_proxy_,
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

  EXPECT_CALL(*pp_proxy_,
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

  EXPECT_CALL(*pp_proxy_,
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

  EXPECT_CALL(*pp_proxy_,
              BruschettaVmShutdown(
                  Property(&BruschettaVmShutdownRequest::id, id), _, _, _))
      .WillOnce(Return(true));

  const bool result = client_->NotifyBruschettaVmShutdown(id);
  EXPECT_TRUE(result);
}

TEST_F(ClientTest, NotifyBorealisVmStartup) {
  const uint32_t id = 6;
  const std::string tap_ifname = "vmtap3";
  const auto borealis_ipv4_subnet =
      *net_base::IPv4CIDR::CreateFromCIDRString("100.115.93.0/29");
  const auto borealis_ipv4_address =
      *net_base::IPv4Address::CreateFromString("100.115.93.2");
  const auto gateway_ipv4_address =
      *net_base::IPv4Address::CreateFromString("100.115.93.1");

  BorealisVmStartupResponse response_proto;
  response_proto.set_tap_device_ifname(tap_ifname);
  auto* response_subnet = response_proto.mutable_ipv4_subnet();
  response_subnet->set_addr(borealis_ipv4_subnet.address().ToByteString());
  response_subnet->set_prefix_len(
      static_cast<uint32_t>(borealis_ipv4_subnet.prefix_length()));
  response_proto.set_ipv4_address(borealis_ipv4_address.ToByteString());
  response_proto.set_gateway_ipv4_address(gateway_ipv4_address.ToByteString());

  EXPECT_CALL(
      *pp_proxy_,
      BorealisVmStartup(Property(&BorealisVmStartupRequest::id, id), _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(response_proto), Return(true)));

  auto result = client_->NotifyBorealisVmStartup(id);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->tap_device_ifname, tap_ifname);
  EXPECT_EQ(result->borealis_ipv4_subnet, borealis_ipv4_subnet);
  EXPECT_EQ(result->borealis_ipv4_address, borealis_ipv4_address);
  EXPECT_EQ(result->gateway_ipv4_address, gateway_ipv4_address);
}

TEST_F(ClientTest, NotifyBorealisVmShutdown) {
  const uint32_t id = 6;

  EXPECT_CALL(
      *pp_proxy_,
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
  EXPECT_CALL(
      *pp_proxy_,
      ConnectNamespace(Property(&ConnectNamespaceRequest::pid, invalid_pid), _,
                       _, _, _))
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
      *pp_proxy_,
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
  EXPECT_CALL(*pp_proxy_, RegisterNeighborReachabilityEventSignalHandler(_, _))
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
  EXPECT_EQ("CROSTINI_VM",
            Client::TrafficSourceName(Client::TrafficSource::kCrostiniVM));
  EXPECT_EQ("PARALLELS_VM",
            Client::TrafficSourceName(Client::TrafficSource::kParallelsVM));
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
      *pp_proxy_,
      SetFeatureFlag(AllOf(Property(&SetFeatureFlagRequest::enabled, enable),
                           Property(&SetFeatureFlagRequest::flag, flag)),
                     _, _, _))
      .WillOnce(Return(true));

  const bool result =
      client_->SendSetFeatureFlagRequest(Client::FeatureFlag::kWiFiQoS, true);
  EXPECT_TRUE(result);
}

TEST_F(ClientTest, TrafficVector) {
  patchpanel::Client::TrafficVector a = {
      .rx_bytes = 234, .tx_bytes = 78, .rx_packets = 31, .tx_packets = 15};
  patchpanel::Client::TrafficVector b = {
      .rx_bytes = 100, .tx_bytes = 200, .rx_packets = 10, .tx_packets = 20};
  patchpanel::Client::TrafficVector c = {
      .rx_bytes = 334, .tx_bytes = 278, .rx_packets = 41, .tx_packets = 35};

  EXPECT_EQ(c, a + b);
  EXPECT_EQ(c, b + a);
  EXPECT_EQ(c - b, a);
  EXPECT_EQ(c - a, b);
  EXPECT_EQ(b - c, -a);
  EXPECT_EQ(a - c, -b);

  auto d = a;
  d += b;
  EXPECT_EQ(c, d);

  auto e = c;
  e -= a;
  EXPECT_EQ(b, e);
}

TEST_F(ClientTest, SerializeNetworkConfigEmpty) {
  net_base::NetworkConfig input;
  patchpanel::NetworkConfig output;
  SerializeNetworkConfig(input, &output);

  EXPECT_FALSE(output.has_ipv4_address());
  EXPECT_FALSE(output.has_ipv4_broadcast());
  EXPECT_FALSE(output.has_ipv4_gateway());
  EXPECT_EQ(output.ipv6_addresses_size(), 0);
  EXPECT_FALSE(output.has_ipv6_gateway());
  EXPECT_EQ(output.ipv6_delegated_prefixes_size(), 0);
  EXPECT_FALSE(output.ipv6_blackhole_route());
  EXPECT_EQ(output.excluded_route_prefixes_size(), 0);
  EXPECT_EQ(output.included_route_prefixes_size(), 0);
  EXPECT_EQ(output.rfc3442_routes_size(), 0);
  EXPECT_EQ(output.dns_servers_size(), 0);
  EXPECT_EQ(output.dns_search_domains_size(), 0);
  EXPECT_FALSE(output.has_mtu());
  EXPECT_FALSE(output.has_captive_portal_uri());
}

TEST_F(ClientTest, SerializeNetworkConfig) {
  net_base::NetworkConfig input;
  input.ipv4_address =
      *net_base::IPv4CIDR::CreateFromCIDRString("10.0.1.100/24");
  input.ipv4_gateway = *net_base::IPv4Address::CreateFromString("10.0.1.2");
  input.ipv4_broadcast = *net_base::IPv4Address::CreateFromString("10.0.1.255");
  input.ipv6_addresses.push_back(
      *net_base::IPv6CIDR::CreateFromCIDRString("2001:200::1000/64"));
  input.ipv6_addresses.push_back(
      *net_base::IPv6CIDR::CreateFromCIDRString("2001:200::2000/56"));
  input.ipv6_gateway = *net_base::IPv6Address::CreateFromString("2001:200::2");
  input.ipv6_delegated_prefixes.push_back(
      *net_base::IPv6CIDR::CreateFromCIDRString("2001:300:1::/96"));
  input.ipv6_delegated_prefixes.push_back(
      *net_base::IPv6CIDR::CreateFromCIDRString("2001:300:2::/120"));

  input.ipv6_blackhole_route = true;
  input.excluded_route_prefixes.push_back(
      *net_base::IPCIDR::CreateFromCIDRString("2002::/128"));
  input.excluded_route_prefixes.push_back(
      *net_base::IPCIDR::CreateFromCIDRString("1.1.0.0/32"));
  input.included_route_prefixes.push_back(
      *net_base::IPCIDR::CreateFromCIDRString("2002::/120"));
  input.included_route_prefixes.push_back(
      *net_base::IPCIDR::CreateFromCIDRString("1.1.0.0/28"));
  input.rfc3442_routes.emplace_back(
      *net_base::IPv4CIDR::CreateFromCIDRString("2.0.0.0/8"),
      *net_base::IPv4Address::CreateFromString("10.0.1.3"));

  input.dns_servers.push_back(
      *net_base::IPAddress::CreateFromString("8.8.8.8"));
  input.dns_servers.push_back(
      *net_base::IPAddress::CreateFromString("2001:4860::8888"));
  input.dns_search_domains.push_back("google.com");
  input.mtu = 1200;
  input.captive_portal_uri =
      net_base::HttpUrl::CreateFromString("https://portal.net");

  patchpanel::NetworkConfig output;
  SerializeNetworkConfig(input, &output);

  EXPECT_EQ(output.ipv4_address().addr(), std::string({10, 0, 1, 100}));
  EXPECT_EQ(output.ipv4_address().prefix_len(), 24);
  EXPECT_EQ(output.ipv4_broadcast(),
            std::string({10, 0, 1, static_cast<char>(255)}));
  EXPECT_EQ(output.ipv4_gateway(), std::string({10, 0, 1, 2}));

  EXPECT_EQ(output.ipv6_addresses_size(), 2);
  EXPECT_EQ(
      output.ipv6_addresses(0).addr(),
      std::string({0x20, 0x01, 0x2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x10, 0}));
  EXPECT_EQ(output.ipv6_addresses(0).prefix_len(), 64);
  EXPECT_EQ(
      output.ipv6_addresses(1).addr(),
      std::string({0x20, 0x01, 0x2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x20, 0}));
  EXPECT_EQ(output.ipv6_addresses(1).prefix_len(), 56);
  EXPECT_EQ(output.ipv6_gateway(), std::string({0x20, 0x01, 0x2, 0, 0, 0, 0, 0,
                                                0, 0, 0, 0, 0, 0, 0, 0x2}));

  EXPECT_EQ(output.ipv6_delegated_prefixes_size(), 2);
  EXPECT_EQ(
      output.ipv6_delegated_prefixes(0).addr(),
      std::string({0x20, 0x01, 0x3, 0, 0, 0x1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}));
  EXPECT_EQ(output.ipv6_delegated_prefixes(0).prefix_len(), 96);
  EXPECT_EQ(
      output.ipv6_delegated_prefixes(1).addr(),
      std::string({0x20, 0x01, 0x3, 0, 0, 0x2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}));
  EXPECT_EQ(output.ipv6_delegated_prefixes(1).prefix_len(), 120);

  EXPECT_TRUE(output.ipv6_blackhole_route());
  EXPECT_EQ(output.excluded_route_prefixes_size(), 2);
  EXPECT_EQ(
      output.excluded_route_prefixes(0).addr(),
      std::string({0x20, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}));
  EXPECT_EQ(output.excluded_route_prefixes(0).prefix_len(), 128);
  EXPECT_EQ(output.excluded_route_prefixes(1).addr(),
            std::string({1, 1, 0, 0}));
  EXPECT_EQ(output.excluded_route_prefixes(1).prefix_len(), 32);
  EXPECT_EQ(output.included_route_prefixes_size(), 2);
  EXPECT_EQ(
      output.included_route_prefixes(0).addr(),
      std::string({0x20, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}));
  EXPECT_EQ(output.included_route_prefixes(0).prefix_len(), 120);
  EXPECT_EQ(output.included_route_prefixes(1).addr(),
            std::string({1, 1, 0, 0}));
  EXPECT_EQ(output.included_route_prefixes(1).prefix_len(), 28);
  EXPECT_EQ(output.rfc3442_routes_size(), 1);
  EXPECT_EQ(output.rfc3442_routes(0).prefix().addr(),
            std::string({2, 0, 0, 0}));
  EXPECT_EQ(output.rfc3442_routes(0).prefix().prefix_len(), 8);
  EXPECT_EQ(output.rfc3442_routes(0).gateway(), std::string({10, 0, 1, 3}));

  EXPECT_EQ(output.dns_servers_size(), 2);
  EXPECT_EQ(output.dns_servers(0), std::string({8, 8, 8, 8}));
  EXPECT_EQ(output.dns_servers(1),
            std::string({0x20, 0x01, 0x48, 0x60, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                         static_cast<char>(0x88), static_cast<char>(0x88)}));
  EXPECT_EQ(output.dns_search_domains_size(), 1);
  EXPECT_EQ(output.dns_search_domains(0), "google.com");
  EXPECT_EQ(output.mtu(), 1200);
  EXPECT_EQ(output.captive_portal_uri(), "https://portal.net");
}

TEST_F(ClientTest, PrepareTagSocket) {
  base::test::SingleThreadTaskEnvironment task_environment;
  patchpanel::TagSocketRequest request;

  base::RepeatingCallback<bool(int)> tag_socket_callback;
  EXPECT_CALL(*http_transport_, SetSockOptCallback)
      .WillOnce(SaveArg<0>(&tag_socket_callback));
  EXPECT_CALL(*ss_proxy_, TagSocket)
      .WillOnce(DoAll(SaveArg<0>(&request), Return(true)));

  patchpanel::Client::TrafficAnnotation annotation;
  annotation.id = Client::TrafficAnnotationId::kUnspecified;
  client_->PrepareTagSocket(annotation, http_transport_);
  EXPECT_TRUE(tag_socket_callback.Run(0));

  EXPECT_TRUE(request.has_traffic_annotation());
  EXPECT_EQ(request.traffic_annotation().host_id(),
            traffic_annotation::TrafficAnnotation_Id::
                TrafficAnnotation_Id_UNSPECIFIED);
}

TEST_F(ClientTest, TagSocketNetworkId) {
  const int net_id = 6;
  TagSocketResponse response_proto;
  response_proto.set_success(true);

  EXPECT_CALL(
      *ss_proxy_,
      TagSocket(Property(&TagSocketRequest::network_id, net_id), _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(response_proto), Return(true)));

  const auto result = client_->TagSocket(base::ScopedFD(-1), net_id,
                                         std::nullopt, std::nullopt);
  EXPECT_TRUE(result);
}

TEST_F(ClientTest, TagSocketVpnPolicy) {
  TagSocketResponse response_proto;
  response_proto.set_success(true);

  EXPECT_CALL(*ss_proxy_, TagSocket(Property(&TagSocketRequest::vpn_policy,
                                             TagSocketRequest::DEFAULT_ROUTING),
                                    _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(response_proto), Return(true)));

  const auto result = client_->TagSocket(
      base::ScopedFD(-1), std::nullopt,
      Client::VpnRoutingPolicy::kDefaultRouting, std::nullopt);
  EXPECT_TRUE(result);
}

TEST_F(ClientTest, TagSocketTrafficAnnotation) {
  TagSocketResponse response_proto;
  response_proto.set_success(true);

  EXPECT_CALL(*ss_proxy_, TagSocket(_, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(response_proto), Return(true)));

  Client::TrafficAnnotation annotation;
  annotation.id = Client::TrafficAnnotationId::kUnspecified;
  const auto result = client_->TagSocket(base::ScopedFD(-1), std::nullopt,
                                         std::nullopt, std::move(annotation));
  EXPECT_TRUE(result);
}

TEST_F(ClientTest, TagSocketFail) {
  const int net_id = 3;
  TagSocketResponse response_proto;
  response_proto.set_success(false);

  EXPECT_CALL(
      *ss_proxy_,
      TagSocket(Property(&TagSocketRequest::network_id, net_id), _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(response_proto), Return(false)));

  const auto result = client_->TagSocket(base::ScopedFD(-1), net_id,
                                         std::nullopt, std::nullopt);
  EXPECT_FALSE(result);
}

TEST_F(ClientTest, TrafficVectorInitialization) {
  Client::TrafficVector v;
  EXPECT_EQ(0, v.rx_bytes);
  EXPECT_EQ(0, v.tx_bytes);
  EXPECT_EQ(0, v.rx_packets);
  EXPECT_EQ(0, v.tx_packets);
}
}  // namespace
}  // namespace patchpanel
