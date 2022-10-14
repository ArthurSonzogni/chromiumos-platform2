// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <gmock/gmock.h>
#include <gmock/gmock-matchers.h>
#include <gtest/gtest.h>

#include "patchpanel/fake_shill_client.h"
#include "patchpanel/fake_system.h"
#include "patchpanel/guest_ipv6_service.h"
#include "patchpanel/mock_datapath.h"
#include "patchpanel/shill_client.h"

using ::testing::Args;

namespace patchpanel {
namespace {

class GuestIPv6ServiceUnderTest : public GuestIPv6Service {
 public:
  GuestIPv6ServiceUnderTest(Datapath* datapath,
                            ShillClient* shill_client,
                            System* system)
      : GuestIPv6Service(nullptr, datapath, shill_client, system) {}

  MOCK_METHOD3(SendNDProxyControl,
               void(NDProxyControlMessage::NDProxyRequestType type,
                    int32_t if_id_primary,
                    int32_t if_id_secondary));

  void FakeNDProxyNeighborDetectionSignal(int if_id, const in6_addr& ip6addr) {
    NeighborDetectedSignal msg;
    msg.set_if_id(if_id);
    msg.set_ip(&ip6addr, sizeof(in6_addr));
    NDProxySignalMessage nm;
    *nm.mutable_neighbor_detected_signal() = msg;
    FeedbackMessage fm;
    *fm.mutable_ndproxy_signal() = nm;
    OnNDProxyMessage(fm);
  }
};

}  // namespace

class GuestIPv6ServiceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    shill_client_ = shill_helper_.FakeClient();
    shill_client_->SetFakeDeviceProperties(
        "up1", ShillClient::Device{
                   ShillClient::Device::Type::kEthernet, 1, "up1", "", {}});
    shill_client_->SetFakeDeviceProperties(
        "up2", ShillClient::Device{
                   ShillClient::Device::Type::kEthernet, 2, "up2", "", {}});

    system_ = std::make_unique<FakeSystem>();
    datapath_ = std::make_unique<MockDatapath>();
    ON_CALL(*datapath_, MaskInterfaceFlags).WillByDefault(Return(true));
  }

  FakeShillClientHelper shill_helper_;
  std::unique_ptr<FakeShillClient> shill_client_;
  std::unique_ptr<FakeSystem> system_;
  std::unique_ptr<MockDatapath> datapath_;
};

TEST_F(GuestIPv6ServiceTest, SingleUpstreamSingleDownstream) {
  GuestIPv6ServiceUnderTest target(datapath_.get(), shill_client_.get(),
                                   system_.get());
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
  target.StartForwarding("up1", "down1");

  // This should work even IfNametoindex is returning 0 (netdevices can be
  // already gone when StopForwarding() being called).
  ON_CALL(*system_, IfNametoindex("up1")).WillByDefault(Return(0));
  ON_CALL(*system_, IfNametoindex("down1")).WillByDefault(Return(0));
  EXPECT_CALL(target,
              SendNDProxyControl(NDProxyControlMessage::STOP_PROXY, 1, 101));
  target.StopForwarding("up1", "down1");

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
  target.StartForwarding("up1", "down1");

  EXPECT_CALL(target,
              SendNDProxyControl(NDProxyControlMessage::STOP_PROXY, 1, 101));
  target.StopUplink("up1");
}

MATCHER_P2(AreTheseTwo, a, b, "") {
  return (a == std::get<0>(arg) && b == std::get<1>(arg)) ||
         (b == std::get<0>(arg) && a == std::get<1>(arg));
}

TEST_F(GuestIPv6ServiceTest, MultipleUpstreamMultipleDownstream) {
  GuestIPv6ServiceUnderTest target(datapath_.get(), shill_client_.get(),
                                   system_.get());
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
  target.StartForwarding("up1", "down1");
  EXPECT_CALL(
      target,
      SendNDProxyControl(
          NDProxyControlMessage::START_NS_NA_RS_RA_MODIFYING_ROUTER_ADDRESS, 2,
          102));
  target.StartForwarding("up2", "down2");

  EXPECT_CALL(
      target,
      SendNDProxyControl(
          NDProxyControlMessage::START_NS_NA_RS_RA_MODIFYING_ROUTER_ADDRESS, 1,
          103));
  EXPECT_CALL(target,
              SendNDProxyControl(NDProxyControlMessage::START_NS_NA, _, _))
      .With(Args<1, 2>(AreTheseTwo(101, 103)));
  target.StartForwarding("up1", "down3");

  EXPECT_CALL(target,
              SendNDProxyControl(NDProxyControlMessage::STOP_PROXY, _, _))
      .With(Args<1, 2>(AreTheseTwo(1, 103)));
  EXPECT_CALL(target,
              SendNDProxyControl(NDProxyControlMessage::STOP_PROXY, _, _))
      .With(Args<1, 2>(AreTheseTwo(101, 103)));
  target.StopForwarding("up1", "down3");

  EXPECT_CALL(
      target,
      SendNDProxyControl(
          NDProxyControlMessage::START_NS_NA_RS_RA_MODIFYING_ROUTER_ADDRESS, 2,
          103));
  EXPECT_CALL(target,
              SendNDProxyControl(NDProxyControlMessage::START_NS_NA, _, _))
      .With(Args<1, 2>(AreTheseTwo(102, 103)));
  target.StartForwarding("up2", "down3");

  EXPECT_CALL(target,
              SendNDProxyControl(NDProxyControlMessage::STOP_PROXY, _, _))
      .With(Args<1, 2>(AreTheseTwo(2, 102)));
  EXPECT_CALL(target,
              SendNDProxyControl(NDProxyControlMessage::STOP_PROXY, _, _))
      .With(Args<1, 2>(AreTheseTwo(2, 103)));
  EXPECT_CALL(target,
              SendNDProxyControl(NDProxyControlMessage::STOP_PROXY, _, _))
      .With(Args<1, 2>(AreTheseTwo(102, 103)));
  target.StopUplink("up2");
}

TEST_F(GuestIPv6ServiceTest, AdditionalDatapathSetup) {
  GuestIPv6ServiceUnderTest target(datapath_.get(), shill_client_.get(),
                                   system_.get());
  ON_CALL(*system_, IfNametoindex("up1")).WillByDefault(Return(1));
  ON_CALL(*system_, IfNametoindex("up2")).WillByDefault(Return(2));
  ON_CALL(*system_, IfNametoindex("down1")).WillByDefault(Return(101));
  ON_CALL(*system_, IfIndextoname(101)).WillByDefault(Return("down1"));

  // StartForwarding() and OnUplinkIPv6Changed() can be triggered in different
  // order in different scenario so we need to verify both.
  EXPECT_CALL(
      target,
      SendNDProxyControl(
          NDProxyControlMessage::START_NS_NA_RS_RA_MODIFYING_ROUTER_ADDRESS, 1,
          101));
  target.StartForwarding("up1", "down1");

  EXPECT_CALL(*datapath_, AddIPv6NeighborProxy("down1", "2001:db8:0:100::1234"))
      .WillOnce(Return(true));
  target.OnUplinkIPv6Changed("up1", "2001:db8:0:100::1234");

  EXPECT_CALL(*datapath_, AddIPv6HostRoute("down1", "2001:db8:0:100::abcd", 128,
                                           "2001:db8:0:100::1234"));
  target.FakeNDProxyNeighborDetectionSignal(
      101, StringToIPv6Address("2001:db8:0:100::abcd"));

  EXPECT_CALL(target,
              SendNDProxyControl(NDProxyControlMessage::STOP_PROXY, _, _))
      .With(Args<1, 2>(AreTheseTwo(1, 101)));
  EXPECT_CALL(*datapath_,
              RemoveIPv6NeighborProxy("down1", "2001:db8:0:100::1234"));
  EXPECT_CALL(*datapath_, RemoveIPv6HostRoute("2001:db8:0:100::abcd", 128));
  target.StopForwarding("up1", "down1");

  // OnUplinkIPv6Changed -> StartForwarding
  target.OnUplinkIPv6Changed("up1", "2001:db8:0:200::1234");

  EXPECT_CALL(
      target,
      SendNDProxyControl(
          NDProxyControlMessage::START_NS_NA_RS_RA_MODIFYING_ROUTER_ADDRESS, 1,
          101));
  EXPECT_CALL(*datapath_, AddIPv6NeighborProxy("down1", "2001:db8:0:200::1234"))
      .WillOnce(Return(true));
  target.StartForwarding("up1", "down1");

  EXPECT_CALL(*datapath_, AddIPv6HostRoute("down1", "2001:db8:0:200::abcd", 128,
                                           "2001:db8:0:200::1234"));
  target.FakeNDProxyNeighborDetectionSignal(
      101, StringToIPv6Address("2001:db8:0:200::abcd"));

  EXPECT_CALL(*datapath_, AddIPv6HostRoute("down1", "2001:db8:0:200::9876", 128,
                                           "2001:db8:0:200::1234"));
  target.FakeNDProxyNeighborDetectionSignal(
      101, StringToIPv6Address("2001:db8:0:200::9876"));

  EXPECT_CALL(target,
              SendNDProxyControl(NDProxyControlMessage::STOP_PROXY, _, _))
      .With(Args<1, 2>(AreTheseTwo(1, 101)));
  EXPECT_CALL(*datapath_, RemoveIPv6HostRoute("2001:db8:0:200::abcd", 128));
  EXPECT_CALL(*datapath_, RemoveIPv6HostRoute("2001:db8:0:200::9876", 128));
  EXPECT_CALL(*datapath_,
              RemoveIPv6NeighborProxy("down1", "2001:db8:0:200::1234"));
  target.StopUplink("up1");
}

}  // namespace patchpanel
