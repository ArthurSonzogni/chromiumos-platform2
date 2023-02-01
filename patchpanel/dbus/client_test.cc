// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/dbus/client.h"

#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <base/functional/bind.h>
// Ignore Wconversion warnings in libbase headers.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#include <base/test/task_environment.h>
#pragma GCC diagnostic pop
#include <base/task/sequenced_task_runner.h>
#include <chromeos/dbus/service_constants.h>
// Ignore Wconversion warnings in dbus headers.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#include <dbus/message.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_object_proxy.h>
#include <dbus/object_path.h>
#pragma GCC diagnostic pop
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <patchpanel/proto_bindings/patchpanel_service.pb.h>

#include "patchpanel/net_util.h"

namespace patchpanel {
namespace {

using ::testing::_;
using ::testing::ByMove;
using ::testing::Return;
using ::testing::SaveArg;

void ExpectProtoRequestResponse(scoped_refptr<dbus::MockObjectProxy> proxy,
                                google::protobuf::MessageLite* request_proto,
                                google::protobuf::MessageLite* response_proto) {
  EXPECT_CALL(*proxy, CallMethodAndBlock(_, _))
      .WillOnce([request_proto, response_proto](
                    dbus::MethodCall* method_call,
                    int timeout_ms) -> std::unique_ptr<dbus::Response> {
        auto response = dbus::Response::CreateEmpty();
        dbus::MessageReader(method_call).PopArrayOfBytesAsProto(request_proto);
        dbus::MessageWriter(response.get())
            .AppendProtoAsArrayOfBytes(*response_proto);
        return response;
      });
}

class ClientTest : public testing::Test {
 protected:
  ClientTest()
      : dbus_(new dbus::MockBus{dbus::Bus::Options{}}),
        proxy_(new dbus::MockObjectProxy(
            dbus_.get(),
            kPatchPanelServiceName,
            dbus::ObjectPath(kPatchPanelServicePath))),
        client_(Client::New(dbus_, proxy_.get())) {}
  ~ClientTest() { dbus_->ShutdownAndBlock(); }

  void SetUp() override {
    EXPECT_CALL(*dbus_, GetDBusTaskRunner())
        .WillRepeatedly(
            Return(base::SequencedTaskRunner::GetCurrentDefault().get()));
  }

  base::test::TaskEnvironment task_environment_;
  scoped_refptr<dbus::MockBus> dbus_;
  scoped_refptr<dbus::MockObjectProxy> proxy_;
  std::unique_ptr<Client> client_;
};

TEST_F(ClientTest, NotifyArcStartup) {
  pid_t pid = 3456;
  patchpanel::ArcStartupRequest request_proto;
  patchpanel::ArcStartupResponse response_proto;
  ExpectProtoRequestResponse(proxy_, &request_proto, &response_proto);
  bool result = client_->NotifyArcStartup(pid);
  EXPECT_TRUE(result);
  EXPECT_EQ(pid, request_proto.pid());
}

TEST_F(ClientTest, NotifyArcShutdown) {
  patchpanel::ArcShutdownRequest request_proto;
  patchpanel::ArcShutdownResponse response_proto;
  ExpectProtoRequestResponse(proxy_, &request_proto, &response_proto);
  bool result = client_->NotifyArcShutdown();
  EXPECT_TRUE(result);
}

TEST_F(ClientTest, NotifyArcVmStartup) {
  uint32_t cid = 5;

  patchpanel::ArcVmStartupRequest request_proto;
  patchpanel::ArcVmStartupResponse response_proto;
  // FIXME: add virtual devices to the response_proto.
  ExpectProtoRequestResponse(proxy_, &request_proto, &response_proto);
  auto virtual_devices = client_->NotifyArcVmStartup(cid);
  EXPECT_TRUE(virtual_devices.empty());
  EXPECT_EQ(cid, request_proto.cid());
}

TEST_F(ClientTest, NotifyArcVmShutdown) {
  uint32_t cid = 5;
  patchpanel::ArcVmShutdownRequest request_proto;
  patchpanel::ArcVmShutdownResponse response_proto;
  ExpectProtoRequestResponse(proxy_, &request_proto, &response_proto);
  bool result = client_->NotifyArcVmShutdown(cid);
  EXPECT_TRUE(result);
  EXPECT_EQ(cid, request_proto.cid());
}

TEST_F(ClientTest, NotifyTerminaVmStartup) {
  patchpanel::TerminaVmStartupRequest request_proto;
  patchpanel::TerminaVmStartupResponse response_proto;
  auto* response_device = response_proto.mutable_device();
  response_device->set_ifname("vmtap1");
  response_device->set_phys_ifname("wlan0");
  response_device->set_guest_ifname("not_defined");
  response_device->set_ipv4_addr(Ipv4Addr(100, 115, 92, 18));
  response_device->set_host_ipv4_addr(Ipv4Addr(100, 115, 92, 17));
  auto* response_device_subnet = response_device->mutable_ipv4_subnet();
  response_device_subnet->set_addr(
      std::vector<uint8_t>{100, 115, 92, 16}.data(), 4);
  response_device_subnet->set_prefix_len(30);
  response_device->set_guest_type(patchpanel::NetworkDevice::TERMINA_VM);
  response_device->set_dns_proxy_ipv4_addr(
      std::vector<uint8_t>{100, 115, 93, 1}.data(), 4);
  response_device->set_dns_proxy_ipv6_addr(
      std::vector<uint8_t>{0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0x12,
                           0x34, 0xab, 0xcd}
          .data(),
      16);
  auto* response_subnet = response_proto.mutable_container_subnet();
  response_subnet->set_addr(std::vector<uint8_t>{100, 115, 92, 128}.data(), 4);
  response_subnet->set_prefix_len(24);
  ExpectProtoRequestResponse(proxy_, &request_proto, &response_proto);

  uint32_t cid = 5;
  Client::VirtualDevice device;
  Client::IPv4Subnet container_subnet;
  bool result =
      client_->NotifyTerminaVmStartup(cid, &device, &container_subnet);
  EXPECT_TRUE(result);
  EXPECT_EQ(cid, request_proto.cid());
  EXPECT_EQ("vmtap1", device.ifname);
  EXPECT_EQ("wlan0", device.phys_ifname);
  EXPECT_EQ("not_defined", device.guest_ifname);
  EXPECT_EQ("100.115.92.18", IPv4AddressToString(device.ipv4_addr));
  EXPECT_EQ("100.115.92.17", IPv4AddressToString(device.host_ipv4_addr));
  EXPECT_EQ("100.115.92.16", IPv4AddressToString(device.ipv4_subnet.base_addr));
  EXPECT_EQ(30, device.ipv4_subnet.prefix_len);
  EXPECT_EQ(Client::GuestType::kTerminaVm, device.guest_type);
  EXPECT_EQ("100.115.93.1", IPv4AddressToString(device.dns_proxy_ipv4_addr));
  EXPECT_EQ("2001:db8::1234:abcd",
            IPv6AddressToString(device.dns_proxy_ipv6_addr));
  EXPECT_EQ("100.115.92.128", IPv4AddressToString(container_subnet.base_addr));
  EXPECT_EQ(24, container_subnet.prefix_len);
}

TEST_F(ClientTest, NotifyTerminaVmShutdown) {
  uint32_t cid = 5;
  patchpanel::TerminaVmShutdownRequest request_proto;
  patchpanel::TerminaVmShutdownResponse response_proto;
  ExpectProtoRequestResponse(proxy_, &request_proto, &response_proto);
  bool result = client_->NotifyTerminaVmShutdown(cid);
  EXPECT_TRUE(result);
  EXPECT_EQ(cid, request_proto.cid());
}

TEST_F(ClientTest, NotifyPluginVmStartup) {
  patchpanel::PluginVmStartupRequest request_proto;
  patchpanel::PluginVmStartupResponse response_proto;
  auto* response_device = response_proto.mutable_device();
  response_device->set_ifname("vmtap2");
  response_device->set_phys_ifname("eth0");
  response_device->set_guest_ifname("not_defined");
  response_device->set_ipv4_addr(Ipv4Addr(100, 115, 93, 34));
  response_device->set_host_ipv4_addr(Ipv4Addr(100, 115, 93, 33));
  auto* response_device_subnet = response_device->mutable_ipv4_subnet();
  response_device_subnet->set_addr(
      std::vector<uint8_t>{100, 115, 93, 32}.data(), 4);
  response_device_subnet->set_prefix_len(28);
  response_device->set_guest_type(patchpanel::NetworkDevice::PLUGIN_VM);
  response_device->set_dns_proxy_ipv4_addr(
      std::vector<uint8_t>{100, 115, 93, 5}.data(), 4);
  response_device->set_dns_proxy_ipv6_addr(
      std::vector<uint8_t>{0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0xbf,
                           0xc7, 0x4a, 0xd2}
          .data(),
      16);
  ExpectProtoRequestResponse(proxy_, &request_proto, &response_proto);

  uint64_t id = 5;
  int subnet_index = 4;
  Client::VirtualDevice device;
  bool result = client_->NotifyPluginVmStartup(id, subnet_index, &device);
  EXPECT_TRUE(result);
  EXPECT_EQ(id, request_proto.id());
  EXPECT_EQ(subnet_index, request_proto.subnet_index());
  EXPECT_EQ("vmtap2", device.ifname);
  EXPECT_EQ("eth0", device.phys_ifname);
  EXPECT_EQ("not_defined", device.guest_ifname);
  EXPECT_EQ("100.115.93.34", IPv4AddressToString(device.ipv4_addr));
  EXPECT_EQ("100.115.93.33", IPv4AddressToString(device.host_ipv4_addr));
  EXPECT_EQ("100.115.93.32", IPv4AddressToString(device.ipv4_subnet.base_addr));
  EXPECT_EQ(28, device.ipv4_subnet.prefix_len);
  EXPECT_EQ(Client::GuestType::kPluginVm, device.guest_type);
  EXPECT_EQ("100.115.93.5", IPv4AddressToString(device.dns_proxy_ipv4_addr));
  EXPECT_EQ("2001:db8::bfc7:4ad2",
            IPv6AddressToString(device.dns_proxy_ipv6_addr));
}

TEST_F(ClientTest, NotifyPluginVmShutdown) {
  uint64_t id = 5;
  patchpanel::PluginVmShutdownRequest request_proto;
  patchpanel::PluginVmShutdownResponse response_proto;
  ExpectProtoRequestResponse(proxy_, &request_proto, &response_proto);
  bool result = client_->NotifyPluginVmShutdown(id);
  EXPECT_TRUE(result);
  EXPECT_EQ(id, request_proto.id());
}

TEST_F(ClientTest, ConnectNamespace) {
  pid_t pid = 3456;
  std::string outbound_ifname = "";

  // Failure case - invalid pid
  auto result = client_->ConnectNamespace(pid, outbound_ifname, false, true,
                                          Client::TrafficSource::kSystem);
  EXPECT_FALSE(result.first.is_valid());
  EXPECT_TRUE(result.second.peer_ifname.empty());
  EXPECT_TRUE(result.second.host_ifname.empty());
  EXPECT_EQ("", IPv4AddressToString(result.second.peer_ipv4_address));
  EXPECT_EQ("", IPv4AddressToString(result.second.host_ipv4_address));
  EXPECT_EQ("", IPv4AddressToString(result.second.ipv4_subnet.base_addr));
  EXPECT_EQ(0, result.second.ipv4_subnet.prefix_len);

  // Failure case - prohibited pid
  result = client_->ConnectNamespace(1, outbound_ifname, false, true,
                                     Client::TrafficSource::kSystem);
  EXPECT_FALSE(result.first.is_valid());

  // Success case
  patchpanel::ConnectNamespaceResponse response_proto;
  response_proto.set_peer_ifname("veth0");
  response_proto.set_host_ifname("arc_ns0");
  response_proto.set_peer_ipv4_address(Ipv4Addr(100, 115, 92, 130));
  response_proto.set_host_ipv4_address(Ipv4Addr(100, 115, 92, 129));
  auto* response_subnet = response_proto.mutable_ipv4_subnet();
  response_subnet->set_prefix_len(30);
  response_subnet->set_addr(std::vector<uint8_t>{100, 115, 92, 128}.data(), 4);
  std::unique_ptr<dbus::Response> response = dbus::Response::CreateEmpty();
  dbus::MessageWriter response_writer(response.get());
  response_writer.AppendProtoAsArrayOfBytes(response_proto);
  EXPECT_CALL(*proxy_, CallMethodAndBlock(_, _))
      .WillOnce(Return(ByMove(std::move(response))));

  result = client_->ConnectNamespace(pid, outbound_ifname, false, true,
                                     Client::TrafficSource::kSystem);
  EXPECT_TRUE(result.first.is_valid());
  EXPECT_EQ("arc_ns0", result.second.host_ifname);
  EXPECT_EQ("veth0", result.second.peer_ifname);
  EXPECT_EQ(30, result.second.ipv4_subnet.prefix_len);
  EXPECT_EQ("100.115.92.128",
            IPv4AddressToString(result.second.ipv4_subnet.base_addr));
  EXPECT_EQ("100.115.92.129",
            IPv4AddressToString(result.second.host_ipv4_address));
  EXPECT_EQ("100.115.92.130",
            IPv4AddressToString(result.second.peer_ipv4_address));
}

TEST_F(ClientTest, RegisterNeighborEventHandler) {
  static Client::NeighborReachabilityEvent actual_event;
  static int call_num = 0;
  auto callback =
      base::BindRepeating([](const Client::NeighborReachabilityEvent& event) {
        call_num++;
        actual_event = event;
      });

  base::RepeatingCallback<void(dbus::Signal * signal)> registered_dbus_callback;

  EXPECT_CALL(*proxy_,
              DoConnectToSignal(kPatchPanelInterface,
                                kNeighborReachabilityEventSignal, _, _))
      .WillOnce(SaveArg<2>(&registered_dbus_callback));
  client_->RegisterNeighborReachabilityEventHandler(callback);

  NeighborReachabilityEventSignal signal_proto;
  signal_proto.set_ifindex(7);
  signal_proto.set_ip_addr("1.2.3.4");
  signal_proto.set_role(NeighborReachabilityEventSignal::GATEWAY);
  signal_proto.set_type(NeighborReachabilityEventSignal::FAILED);
  dbus::Signal signal(kPatchPanelInterface, kNeighborReachabilityEventSignal);
  dbus::MessageWriter writer(&signal);
  writer.AppendProtoAsArrayOfBytes(signal_proto);

  registered_dbus_callback.Run(&signal);

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
  EXPECT_EQ("PLUGINVM",
            Client::TrafficSourceName(Client::TrafficSource::kPluginVm));
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

}  // namespace
}  // namespace patchpanel
