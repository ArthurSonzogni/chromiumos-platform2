// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <dbus/object_path.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "patchpanel/fake_system.h"
#include "patchpanel/guest_ipv6_service.h"
#include "patchpanel/mock_datapath.h"
#include "patchpanel/shill_client.h"

using testing::_;
using testing::Args;
using testing::Return;

namespace patchpanel {
namespace {

class GuestIPv6ServiceUnderTest : public GuestIPv6Service {
 public:
  GuestIPv6ServiceUnderTest(Datapath* datapath, System* system)
      : GuestIPv6Service(nullptr, datapath, system) {}

  MOCK_METHOD(void,
              SendNDProxyControl,
              (NDProxyControlMessage::NDProxyRequestType type,
               int32_t if_id_primary,
               int32_t if_id_secondary),
              (override));
  MOCK_METHOD(bool,
              StartRAServer,
              (const std::string& ifname,
               const net_base::IPv6CIDR& prefix,
               const std::vector<std::string>& rdnss,
               const std::optional<int>& mtu,
               const std::optional<int>& hop_limit),
              (override));
  MOCK_METHOD(bool, StopRAServer, (const std::string& ifname), (override));

  void FakeNDProxyNeighborDetectionSignal(
      int if_id, const net_base::IPv6Address& ip6addr) {
    NeighborDetectedSignal msg;
    msg.set_if_id(if_id);
    msg.set_ip(ip6addr.ToByteString());
    NDProxySignalMessage nm;
    *nm.mutable_neighbor_detected_signal() = msg;
    FeedbackMessage fm;
    *fm.mutable_ndproxy_signal() = nm;
    OnNDProxyMessage(fm);
  }

  void TriggerCreateConfigFile(const std::string& ifname,
                               const net_base::IPv6CIDR& prefix,
                               const std::vector<std::string>& rdnss,
                               const std::optional<int>& mtu,
                               const std::optional<int>& hop_limit) {
    GuestIPv6Service::CreateConfigFile(ifname, prefix, rdnss, mtu, hop_limit);
  }
};

ShillClient::Device MakeFakeShillDevice(const std::string& ifname,
                                        int ifindex) {
  ShillClient::Device dev;
  dev.type = ShillClient::Device::Type::kEthernet;
  dev.ifindex = ifindex;
  dev.ifname = ifname;
  dev.service_path = "/service/" + std::to_string(ifindex);
  return dev;
}

class GuestIPv6ServiceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    system_ = std::make_unique<FakeSystem>();
    datapath_ = std::make_unique<MockDatapath>();
    ON_CALL(*datapath_, MaskInterfaceFlags).WillByDefault(Return(true));
  }

  std::unique_ptr<FakeSystem> system_;
  std::unique_ptr<MockDatapath> datapath_;
};

TEST_F(GuestIPv6ServiceTest, SingleUpstreamSingleDownstream) {
  auto up1_dev = MakeFakeShillDevice("up1", 1);
  GuestIPv6ServiceUnderTest target(datapath_.get(), system_.get());
  EXPECT_CALL(*system_, IfNametoindex("up1")).WillOnce(Return(1));
  EXPECT_CALL(*system_, IfNametoindex("down1")).WillOnce(Return(101));
  EXPECT_CALL(*datapath_, MaskInterfaceFlags("up1", IFF_ALLMULTI, 0))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, MaskInterfaceFlags("down1", IFF_ALLMULTI, 0))
      .WillOnce(Return(true));

  EXPECT_CALL(
      target,
      SendNDProxyControl(
          NDProxyControlMessage::START_NS_NA_RS_RA_MODIFYING_ROUTER_ADDRESS, 1,
          101));
  target.StartForwarding(up1_dev, "down1");

  // This should work even IfNametoindex is returning 0 (netdevices can be
  // already gone when StopForwarding() being called).
  ON_CALL(*system_, IfNametoindex("up1")).WillByDefault(Return(0));
  ON_CALL(*system_, IfNametoindex("down1")).WillByDefault(Return(0));
  EXPECT_CALL(target,
              SendNDProxyControl(NDProxyControlMessage::STOP_PROXY, 1, 101));
  target.StopForwarding(up1_dev, "down1");

  EXPECT_CALL(*system_, IfNametoindex("up1")).WillOnce(Return(1));
  EXPECT_CALL(*system_, IfNametoindex("down1")).WillOnce(Return(101));
  EXPECT_CALL(*datapath_, MaskInterfaceFlags("up1", IFF_ALLMULTI, 0))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, MaskInterfaceFlags("down1", IFF_ALLMULTI, 0))
      .WillOnce(Return(true));
  EXPECT_CALL(
      target,
      SendNDProxyControl(
          NDProxyControlMessage::START_NS_NA_RS_RA_MODIFYING_ROUTER_ADDRESS, 1,
          101));
  target.StartForwarding(up1_dev, "down1");

  EXPECT_CALL(target,
              SendNDProxyControl(NDProxyControlMessage::STOP_PROXY, 1, 101));
  target.StopUplink(up1_dev);
}

MATCHER_P2(AreTheseTwo, a, b, "") {
  return (a == std::get<0>(arg) && b == std::get<1>(arg)) ||
         (b == std::get<0>(arg) && a == std::get<1>(arg));
}

TEST_F(GuestIPv6ServiceTest, MultipleUpstreamMultipleDownstream) {
  auto up1_dev = MakeFakeShillDevice("up1", 1);
  auto up2_dev = MakeFakeShillDevice("up2", 2);
  GuestIPv6ServiceUnderTest target(datapath_.get(), system_.get());
  ON_CALL(*system_, IfNametoindex("up1")).WillByDefault(Return(1));
  ON_CALL(*system_, IfNametoindex("up2")).WillByDefault(Return(2));
  ON_CALL(*system_, IfNametoindex("down1")).WillByDefault(Return(101));
  ON_CALL(*system_, IfNametoindex("down2")).WillByDefault(Return(102));
  ON_CALL(*system_, IfNametoindex("down3")).WillByDefault(Return(103));

  EXPECT_CALL(
      target,
      SendNDProxyControl(
          NDProxyControlMessage::START_NS_NA_RS_RA_MODIFYING_ROUTER_ADDRESS, 1,
          101));
  target.StartForwarding(up1_dev, "down1");
  EXPECT_CALL(
      target,
      SendNDProxyControl(
          NDProxyControlMessage::START_NS_NA_RS_RA_MODIFYING_ROUTER_ADDRESS, 2,
          102));
  target.StartForwarding(up2_dev, "down2");

  EXPECT_CALL(
      target,
      SendNDProxyControl(
          NDProxyControlMessage::START_NS_NA_RS_RA_MODIFYING_ROUTER_ADDRESS, 1,
          103));
  EXPECT_CALL(target,
              SendNDProxyControl(NDProxyControlMessage::START_NS_NA, _, _))
      .With(Args<1, 2>(AreTheseTwo(101, 103)));
  target.StartForwarding(up1_dev, "down3");

  EXPECT_CALL(target,
              SendNDProxyControl(NDProxyControlMessage::STOP_PROXY, _, _))
      .With(Args<1, 2>(AreTheseTwo(1, 103)));
  EXPECT_CALL(target,
              SendNDProxyControl(NDProxyControlMessage::STOP_PROXY, _, _))
      .With(Args<1, 2>(AreTheseTwo(101, 103)));
  target.StopForwarding(up1_dev, "down3");

  EXPECT_CALL(
      target,
      SendNDProxyControl(
          NDProxyControlMessage::START_NS_NA_RS_RA_MODIFYING_ROUTER_ADDRESS, 2,
          103));
  EXPECT_CALL(target,
              SendNDProxyControl(NDProxyControlMessage::START_NS_NA, _, _))
      .With(Args<1, 2>(AreTheseTwo(102, 103)));
  target.StartForwarding(up2_dev, "down3");

  EXPECT_CALL(target,
              SendNDProxyControl(NDProxyControlMessage::STOP_PROXY, _, _))
      .With(Args<1, 2>(AreTheseTwo(2, 102)));
  EXPECT_CALL(target,
              SendNDProxyControl(NDProxyControlMessage::STOP_PROXY, _, _))
      .With(Args<1, 2>(AreTheseTwo(2, 103)));
  EXPECT_CALL(target,
              SendNDProxyControl(NDProxyControlMessage::STOP_PROXY, _, _))
      .With(Args<1, 2>(AreTheseTwo(102, 103)));
  target.StopUplink(up2_dev);
}

TEST_F(GuestIPv6ServiceTest, AdditionalDatapathSetup) {
  auto up1_dev = MakeFakeShillDevice("up1", 1);
  GuestIPv6ServiceUnderTest target(datapath_.get(), system_.get());
  ON_CALL(*system_, IfNametoindex("up1")).WillByDefault(Return(1));
  ON_CALL(*system_, IfNametoindex("down1")).WillByDefault(Return(101));
  ON_CALL(*system_, IfIndextoname(101)).WillByDefault(Return("down1"));

  // StartForwarding() and OnUplinkIPv6Changed() can be triggered in different
  // order in different scenario so we need to verify both.
  EXPECT_CALL(
      target,
      SendNDProxyControl(
          NDProxyControlMessage::START_NS_NA_RS_RA_MODIFYING_ROUTER_ADDRESS, 1,
          101));
  target.StartForwarding(up1_dev, "down1");

  EXPECT_CALL(*datapath_, AddIPv6NeighborProxy(
                              "down1", *net_base::IPv6Address::CreateFromString(
                                           "2001:db8:0:100::1234")))
      .WillOnce(Return(true));
  up1_dev.ipconfig.ipv6_cidr =
      *net_base::IPv6CIDR::CreateFromCIDRString("2001:db8:0:100::1234/64");
  target.OnUplinkIPv6Changed(up1_dev);

  EXPECT_CALL(
      *datapath_,
      AddIPv6HostRoute(
          "down1",
          *net_base::IPv6CIDR::CreateFromCIDRString("2001:db8:0:100::abcd/128"),
          net_base::IPv6Address::CreateFromString("2001:db8:0:100::1234")));
  target.FakeNDProxyNeighborDetectionSignal(
      101, *net_base::IPv6Address::CreateFromString("2001:db8:0:100::abcd"));

  EXPECT_CALL(target,
              SendNDProxyControl(NDProxyControlMessage::STOP_PROXY, _, _))
      .With(Args<1, 2>(AreTheseTwo(1, 101)));
  EXPECT_CALL(*datapath_, RemoveIPv6NeighborProxy(
                              "down1", *net_base::IPv6Address::CreateFromString(
                                           "2001:db8:0:100::1234")));
  EXPECT_CALL(*datapath_,
              RemoveIPv6HostRoute(*net_base::IPv6CIDR::CreateFromCIDRString(
                  "2001:db8:0:100::abcd/128")));
  target.StopForwarding(up1_dev, "down1");

  // OnUplinkIPv6Changed -> StartForwarding
  up1_dev.ipconfig.ipv6_cidr =
      *net_base::IPv6CIDR::CreateFromCIDRString("2001:db8:0:200::1234/64");
  target.OnUplinkIPv6Changed(up1_dev);

  EXPECT_CALL(
      target,
      SendNDProxyControl(
          NDProxyControlMessage::START_NS_NA_RS_RA_MODIFYING_ROUTER_ADDRESS, 1,
          101));
  EXPECT_CALL(*datapath_, AddIPv6NeighborProxy(
                              "down1", *net_base::IPv6Address::CreateFromString(
                                           "2001:db8:0:200::1234")))
      .WillOnce(Return(true));
  target.StartForwarding(up1_dev, "down1");

  EXPECT_CALL(
      *datapath_,
      AddIPv6HostRoute(
          "down1",
          *net_base::IPv6CIDR::CreateFromCIDRString("2001:db8:0:200::abcd/128"),
          net_base::IPv6Address::CreateFromString("2001:db8:0:200::1234")));
  target.FakeNDProxyNeighborDetectionSignal(
      101, *net_base::IPv6Address::CreateFromString("2001:db8:0:200::abcd"));

  EXPECT_CALL(
      *datapath_,
      AddIPv6HostRoute(
          "down1",
          *net_base::IPv6CIDR::CreateFromCIDRString("2001:db8:0:200::9876/128"),
          net_base::IPv6Address::CreateFromString("2001:db8:0:200::1234")));
  target.FakeNDProxyNeighborDetectionSignal(
      101, *net_base::IPv6Address::CreateFromString("2001:db8:0:200::9876"));

  EXPECT_CALL(target,
              SendNDProxyControl(NDProxyControlMessage::STOP_PROXY, _, _))
      .With(Args<1, 2>(AreTheseTwo(1, 101)));
  EXPECT_CALL(*datapath_,
              RemoveIPv6HostRoute(*net_base::IPv6CIDR::CreateFromCIDRString(
                  "2001:db8:0:200::abcd/128")));
  EXPECT_CALL(*datapath_,
              RemoveIPv6HostRoute(*net_base::IPv6CIDR::CreateFromCIDRString(
                  "2001:db8:0:200::9876/128")));
  EXPECT_CALL(*datapath_, RemoveIPv6NeighborProxy(
                              "down1", *net_base::IPv6Address::CreateFromString(
                                           "2001:db8:0:200::1234")));
  target.StopUplink(up1_dev);
}

TEST_F(GuestIPv6ServiceTest, RAServer) {
  auto up1_dev = MakeFakeShillDevice("up1", 1);
  const std::optional<int> mtu = 1450;
  const std::optional<int> hop_limit = 63;
  GuestIPv6ServiceUnderTest target(datapath_.get(), system_.get());
  ON_CALL(*system_, IfNametoindex("up1")).WillByDefault(Return(1));
  ON_CALL(*system_, IfNametoindex("down1")).WillByDefault(Return(101));
  ON_CALL(*system_, IfNametoindex("down2")).WillByDefault(Return(102));

  target.SetForwardMethod(up1_dev,
                          GuestIPv6Service::ForwardMethod::kMethodRAServer);

  EXPECT_CALL(
      target,
      SendNDProxyControl(
          NDProxyControlMessage::START_NS_NA_RS_RA_MODIFYING_ROUTER_ADDRESS, _,
          _))
      .Times(0);
  EXPECT_CALL(target, SendNDProxyControl(
                          NDProxyControlMessage::START_NS_NA_RS_RA, _, _))
      .Times(0);
  EXPECT_CALL(target,
              SendNDProxyControl(NDProxyControlMessage::START_NEIGHBOR_MONITOR,
                                 101, _));
  target.StartForwarding(up1_dev, "down1", mtu, hop_limit);

  EXPECT_CALL(target, StartRAServer("down1",
                                    *net_base::IPv6CIDR::CreateFromCIDRString(
                                        "2001:db8:0:200::/64"),
                                    std::vector<std::string>{}, mtu, hop_limit))
      .WillOnce(Return(true));
  up1_dev.ipconfig.ipv6_cidr =
      *net_base::IPv6CIDR::CreateFromCIDRString("2001:db8:0:200::1234/64");
  target.OnUplinkIPv6Changed(up1_dev);

  EXPECT_CALL(target, StartRAServer("down2",
                                    *net_base::IPv6CIDR::CreateFromCIDRString(
                                        "2001:db8:0:200::/64"),
                                    std::vector<std::string>{}, mtu, hop_limit))
      .WillOnce(Return(true));
  EXPECT_CALL(target,
              SendNDProxyControl(NDProxyControlMessage::START_NS_NA, _, _))
      .With(Args<1, 2>(AreTheseTwo(101, 102)));
  EXPECT_CALL(target,
              SendNDProxyControl(NDProxyControlMessage::START_NEIGHBOR_MONITOR,
                                 102, _));
  // The previously set MTU and CurHopLimit should be used when passing
  // std::nullopt.
  target.StartForwarding(up1_dev, "down2", std::nullopt, std::nullopt);

  EXPECT_CALL(target,
              SendNDProxyControl(NDProxyControlMessage::STOP_PROXY, _, _))
      .With(Args<1, 2>(AreTheseTwo(101, 102)));
  EXPECT_CALL(
      target,
      SendNDProxyControl(NDProxyControlMessage::STOP_NEIGHBOR_MONITOR, 101, _));
  EXPECT_CALL(target, StopRAServer("down1")).WillOnce(Return(true));
  EXPECT_CALL(
      target,
      SendNDProxyControl(NDProxyControlMessage::STOP_NEIGHBOR_MONITOR, 102, _));
  EXPECT_CALL(target, StopRAServer("down2")).WillOnce(Return(true));
  target.StopUplink(up1_dev);
}

TEST_F(GuestIPv6ServiceTest, RAServerUplinkIPChange) {
  auto up1_dev = MakeFakeShillDevice("up1", 1);
  const std::optional<int> mtu = 1450;
  const std::optional<int> hop_limit = 63;
  GuestIPv6ServiceUnderTest target(datapath_.get(), system_.get());
  ON_CALL(*system_, IfNametoindex("up1")).WillByDefault(Return(1));
  ON_CALL(*system_, IfNametoindex("down1")).WillByDefault(Return(101));
  ON_CALL(*system_, IfIndextoname(101)).WillByDefault(Return("down1"));

  target.SetForwardMethod(up1_dev,
                          GuestIPv6Service::ForwardMethod::kMethodRAServer);

  target.StartForwarding(up1_dev, "down1", mtu, hop_limit);

  EXPECT_CALL(target, StartRAServer("down1",
                                    *net_base::IPv6CIDR::CreateFromCIDRString(
                                        "2001:db8:0:200::/64"),
                                    std::vector<std::string>{}, mtu, hop_limit))
      .WillOnce(Return(true));
  up1_dev.ipconfig.ipv6_cidr =
      *net_base::IPv6CIDR::CreateFromCIDRString("2001:db8:0:200::1234/64");
  target.OnUplinkIPv6Changed(up1_dev);

  EXPECT_CALL(target, StopRAServer("down1")).WillOnce(Return(true));
  EXPECT_CALL(target, StartRAServer("down1",
                                    *net_base::IPv6CIDR::CreateFromCIDRString(
                                        "2001:db8:0:100::/64"),
                                    std::vector<std::string>{}, mtu, hop_limit))
      .WillOnce(Return(true));
  up1_dev.ipconfig.ipv6_cidr =
      *net_base::IPv6CIDR::CreateFromCIDRString("2001:db8:0:100::abcd/64");
  target.OnUplinkIPv6Changed(up1_dev);

  // OnUplinkIPv6Changed should cause existing /128 routes to be updated.
  EXPECT_CALL(
      *datapath_,
      AddIPv6HostRoute(
          "down1",
          *net_base::IPv6CIDR::CreateFromCIDRString("2001:db8:0:100::9999/128"),
          net_base::IPv6Address::CreateFromString("2001:db8:0:100::abcd")));
  target.FakeNDProxyNeighborDetectionSignal(
      101, *net_base::IPv6Address::CreateFromString("2001:db8:0:100::9999"));

  EXPECT_CALL(
      *datapath_,
      AddIPv6HostRoute(
          "down1",
          *net_base::IPv6CIDR::CreateFromCIDRString("2001:db8:0:100::9999/128"),
          net_base::IPv6Address::CreateFromString("2001:db8:0:100::1234")));
  up1_dev.ipconfig.ipv6_cidr =
      *net_base::IPv6CIDR::CreateFromCIDRString("2001:db8:0:100::1234/64");
  target.OnUplinkIPv6Changed(up1_dev);

  EXPECT_CALL(target, StopRAServer("down1")).WillOnce(Return(true));
  target.StopUplink(up1_dev);
}

TEST_F(GuestIPv6ServiceTest, RAServerUplinkDNSChange) {
  auto up1_dev = MakeFakeShillDevice("up1", 1);
  const std::optional<int> mtu = 1450;
  const std::optional<int> hop_limit = 63;
  GuestIPv6ServiceUnderTest target(datapath_.get(), system_.get());
  ON_CALL(*system_, IfNametoindex("up1")).WillByDefault(Return(1));
  ON_CALL(*system_, IfNametoindex("down1")).WillByDefault(Return(101));

  target.SetForwardMethod(up1_dev,
                          GuestIPv6Service::ForwardMethod::kMethodRAServer);

  target.StartForwarding(up1_dev, "down1", mtu, hop_limit);

  EXPECT_CALL(target, StartRAServer("down1",
                                    *net_base::IPv6CIDR::CreateFromCIDRString(
                                        "2001:db8:0:200::/64"),
                                    std::vector<std::string>{}, mtu, hop_limit))
      .WillOnce(Return(true));
  up1_dev.ipconfig.ipv6_cidr =
      *net_base::IPv6CIDR::CreateFromCIDRString("2001:db8:0:200::1234/64");
  target.OnUplinkIPv6Changed(up1_dev);

  // Update DNS should trigger RA server restart.
  EXPECT_CALL(target, StopRAServer("down1")).WillOnce(Return(true));
  EXPECT_CALL(
      target,
      StartRAServer(
          "down1",
          *net_base::IPv6CIDR::CreateFromCIDRString("2001:db8:0:200::/64"),
          std::vector<std::string>{"2001:db8:0:cafe::2", "2001:db8:0:cafe::3"},
          mtu, hop_limit))
      .WillOnce(Return(true));
  up1_dev.ipconfig.ipv6_dns_addresses = {"2001:db8:0:cafe::2",
                                         "2001:db8:0:cafe::3"};
  target.UpdateUplinkIPv6DNS(up1_dev);

  // If the content of DNS did not change, no restart should be triggered.
  EXPECT_CALL(target, StopRAServer).Times(0);
  EXPECT_CALL(target, StartRAServer).Times(0);
  up1_dev.ipconfig.ipv6_dns_addresses = {"2001:db8:0:cafe::3",
                                         "2001:db8:0:cafe::2"};
  target.UpdateUplinkIPv6DNS(up1_dev);

  // Removal of a DNS address should trigger RA server restart.
  EXPECT_CALL(target, StopRAServer("down1")).WillOnce(Return(true));
  EXPECT_CALL(
      target,
      StartRAServer(
          "down1",
          *net_base::IPv6CIDR::CreateFromCIDRString("2001:db8:0:200::/64"),
          std::vector<std::string>{"2001:db8:0:cafe::3"}, mtu, hop_limit))
      .WillOnce(Return(true));
  up1_dev.ipconfig.ipv6_dns_addresses = {"2001:db8:0:cafe::3"};
  target.UpdateUplinkIPv6DNS(up1_dev);

  EXPECT_CALL(target, StopRAServer("down1")).WillOnce(Return(true));
  target.StopUplink(up1_dev);
}

TEST_F(GuestIPv6ServiceTest, SetMethodOnTheFly) {
  auto up1_dev = MakeFakeShillDevice("up1", 1);
  const std::optional<int> mtu = 1450;
  const std::optional<int> hop_limit = 63;
  GuestIPv6ServiceUnderTest target(datapath_.get(), system_.get());
  ON_CALL(*system_, IfNametoindex("up1")).WillByDefault(Return(1));
  ON_CALL(*system_, IfNametoindex("down1")).WillByDefault(Return(101));

  up1_dev.ipconfig.ipv6_cidr =
      *net_base::IPv6CIDR::CreateFromCIDRString("2001:db8:0:200::1234/64");
  target.OnUplinkIPv6Changed(up1_dev);

  EXPECT_CALL(
      target,
      SendNDProxyControl(
          NDProxyControlMessage::START_NS_NA_RS_RA_MODIFYING_ROUTER_ADDRESS, 1,
          101));
  target.StartForwarding(up1_dev, "down1", mtu, hop_limit);

  EXPECT_CALL(target,
              SendNDProxyControl(NDProxyControlMessage::STOP_PROXY, 1, 101));
  EXPECT_CALL(target, StartRAServer("down1",
                                    *net_base::IPv6CIDR::CreateFromCIDRString(
                                        "2001:db8:0:200::/64"),
                                    std::vector<std::string>{}, mtu, hop_limit))
      .WillOnce(Return(true));
  EXPECT_CALL(target,
              SendNDProxyControl(NDProxyControlMessage::START_NEIGHBOR_MONITOR,
                                 101, _));
  target.SetForwardMethod(up1_dev,
                          GuestIPv6Service::ForwardMethod::kMethodRAServer);

  EXPECT_CALL(target, StopRAServer("down1")).WillOnce(Return(true));
  EXPECT_CALL(
      target,
      SendNDProxyControl(NDProxyControlMessage::STOP_NEIGHBOR_MONITOR, 101, _));
  target.StopForwarding(up1_dev, "down1");
}

constexpr char kExpectedConfigFile[] = R"(interface eth0 {
  AdvSendAdvert on;
  prefix fd00::/64 {
    AdvOnLink off;
    AdvAutonomous on;
  };
  AdvLinkMTU 1000;
  AdvCurHopLimit 64;
  RDNSS fd00::1 fd00::2 {};
};
)";

TEST_F(GuestIPv6ServiceTest, CreateConfigFile) {
  GuestIPv6ServiceUnderTest target(datapath_.get(), system_.get());
  EXPECT_CALL(*system_, WriteConfigFile(_, kExpectedConfigFile))
      .WillOnce(Return(true));
  target.TriggerCreateConfigFile(
      "eth0", *net_base::IPv6CIDR::CreateFromCIDRString("fd00::/64"),
      {"fd00::1", "fd00::2"},
      /*mtu=*/1000,
      /*hop_limit*/ 64);
}

}  // namespace
}  // namespace patchpanel
