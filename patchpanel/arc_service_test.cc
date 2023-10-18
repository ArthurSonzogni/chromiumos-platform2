// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/arc_service.h"

#include <net/if.h>

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <base/functional/bind.h>
#include <base/functional/callback_helpers.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <metrics/metrics_library_mock.h>
#include <net-base/ipv4_address.h>

#include "patchpanel/address_manager.h"
#include "patchpanel/datapath.h"
#include "patchpanel/mock_datapath.h"
#include "patchpanel/shill_client.h"

using net_base::IPv4Address;
using net_base::IPv4CIDR;
using testing::_;
using testing::AnyNumber;
using testing::Eq;
using testing::Invoke;
using testing::Mock;
using testing::Pair;
using testing::Pointee;
using testing::Property;
using testing::Return;
using testing::ReturnRef;
using testing::StrEq;
using testing::UnorderedElementsAre;

namespace patchpanel {
namespace {
constexpr uint32_t kTestPID = 2;
constexpr uint32_t kTestCID = 2;
const IPv4CIDR kArcHostCIDR =
    *IPv4CIDR::CreateFromCIDRString("100.115.92.1/30");
const IPv4CIDR kArcGuestCIDR =
    *IPv4CIDR::CreateFromCIDRString("100.115.92.2/30");
const IPv4CIDR kFirstEthHostCIDR =
    *IPv4CIDR::CreateFromCIDRString("100.115.92.5/30");
const IPv4Address kFirstEthGuestIP = IPv4Address(100, 115, 92, 6);
const IPv4CIDR kFirstEthGuestCIDR =
    *IPv4CIDR::CreateFromAddressAndPrefix(kFirstEthGuestIP, 30);
const IPv4CIDR kSecondEthHostCIDR =
    *IPv4CIDR::CreateFromCIDRString("100.115.92.9/30");
const IPv4CIDR kFirstWifiHostCIDR =
    *IPv4CIDR::CreateFromCIDRString("100.115.92.13/30");
const IPv4CIDR kSecondWifiHostCIDR =
    *IPv4CIDR::CreateFromCIDRString("100.115.92.17/30");
const IPv4Address kFirstCellGuestIP = IPv4Address(100, 115, 92, 22);
const IPv4CIDR kFirstCellGuestCIDR =
    *IPv4CIDR::CreateFromAddressAndPrefix(kFirstCellGuestIP, 30);
const IPv4CIDR kFirstCellHostCIDR =
    *IPv4CIDR::CreateFromCIDRString("100.115.92.21/30");

ShillClient::Device MakeShillDevice(
    const std::string& shill_device_interface_property,
    ShillClient::Device::Type type,
    std::optional<std::string> primary_multiplexed_interface = std::nullopt) {
  ShillClient::Device dev;
  dev.shill_device_interface_property = shill_device_interface_property;
  dev.primary_multiplexed_interface = primary_multiplexed_interface;
  dev.type = type;
  dev.ifname =
      primary_multiplexed_interface.value_or(shill_device_interface_property);
  return dev;
}

MATCHER_P(IsShillDevice, expected_ifname, "") {
  return !arg.primary_multiplexed_interface.has_value() &&
         arg.ifname == expected_ifname;
}

MATCHER_P2(IsShillMultiplexedDevice,
           expected_shill_device_ifname,
           expected_ifname,
           "") {
  return arg.shill_device_interface_property == expected_shill_device_ifname &&
         arg.ifname == expected_ifname && arg.primary_multiplexed_interface &&
         arg.ifname == expected_ifname;
}

}  // namespace

class ArcServiceTest : public testing::Test {
 public:
  ArcServiceTest() : testing::Test() {}

 protected:
  void SetUp() override {
    datapath_ = std::make_unique<MockDatapath>();
    addr_mgr_ = std::make_unique<AddressManager>();
    metrics_ = std::make_unique<MetricsLibraryMock>();
    guest_device_events_.clear();
    shill_devices_.clear();
  }

  std::unique_ptr<ArcService> NewService(ArcService::ArcType arc_type) {
    return std::make_unique<ArcService>(
        datapath_.get(), addr_mgr_.get(), arc_type, metrics_.get(),
        base::BindRepeating(&ArcServiceTest::ArcDeviceEventHandler,
                            base::Unretained(this)));
  }

  void ArcDeviceEventHandler(const ShillClient::Device& shill_device,
                             const ArcService::ArcDevice& arc_device,
                             ArcService::ArcDeviceEvent event) {
    guest_device_events_[arc_device.bridge_ifname()] = event;
    shill_devices_[arc_device.bridge_ifname()] = shill_device;
  }

  std::unique_ptr<AddressManager> addr_mgr_;
  std::unique_ptr<MockDatapath> datapath_;
  std::unique_ptr<MetricsLibraryMock> metrics_;
  std::map<std::string, ArcService::ArcDeviceEvent> guest_device_events_;
  std::map<std::string, ShillClient::Device> shill_devices_;
};

TEST_F(ArcServiceTest, Arc0IPAddress) {
  auto svc = NewService(ArcService::ArcType::kVM);
  ASSERT_TRUE(svc->GetArc0IPv4Address().has_value());
  EXPECT_EQ(*net_base::IPv4Address::CreateFromString("100.115.92.2"),
            svc->GetArc0IPv4Address());
}

TEST_F(ArcServiceTest, NotStarted_AddDevice) {
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arc_eth0"), _)).Times(0);
  EXPECT_CALL(*datapath_,
              StartRoutingDevice(IsShillDevice("eth0"), StrEq("arc_eth0"),
                                 TrafficSource::kArc,
                                 /*static_ipv6=*/false))
      .Times(0);
  EXPECT_CALL(*datapath_, AddInboundIPv4DNAT(AutoDNATTarget::kArc,
                                             IsShillDevice("eth0"), _))
      .Times(0);

  auto eth_dev = MakeShillDevice("eth0", ShillClient::Device::Type::kEthernet);
  auto svc = NewService(ArcService::ArcType::kContainer);
  svc->AddDevice(eth_dev);
  EXPECT_TRUE(svc->devices_.find("eth0") == svc->devices_.end());
  EXPECT_FALSE(svc->shill_devices_.find("eth0") == svc->shill_devices_.end());
}

TEST_F(ArcServiceTest, NotStarted_AddRemoveDevice) {
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arc_eth0"), _)).Times(0);
  EXPECT_CALL(*datapath_,
              StartRoutingDevice(IsShillDevice("eth0"), StrEq("arc_eth0"),
                                 TrafficSource::kArc,
                                 /*static_ipv6=*/false))
      .Times(0);
  EXPECT_CALL(*datapath_, AddInboundIPv4DNAT(AutoDNATTarget::kArc,
                                             IsShillDevice("eth0"), _))
      .Times(0);
  EXPECT_CALL(*datapath_, StopRoutingDevice(StrEq("arc_eth0"))).Times(0);
  EXPECT_CALL(*datapath_, RemoveInboundIPv4DNAT(AutoDNATTarget::kArc,
                                                IsShillDevice("eth0"), _))
      .Times(0);
  EXPECT_CALL(*datapath_, RemoveBridge(StrEq("arc_eth0"))).Times(0);

  auto eth_dev = MakeShillDevice("eth0", ShillClient::Device::Type::kEthernet);
  auto svc = NewService(ArcService::ArcType::kContainer);
  svc->AddDevice(eth_dev);
  svc->RemoveDevice(eth_dev);
  EXPECT_TRUE(svc->devices_.find("eth0") == svc->devices_.end());
  EXPECT_TRUE(svc->shill_devices_.find("eth0") == svc->shill_devices_.end());
}

TEST_F(ArcServiceTest, VerifyAddrConfigs) {
  EXPECT_CALL(*datapath_, NetnsAttachName(StrEq("arc_netns"), kTestPID))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arcbr0"), kArcHostCIDR))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arc_eth0"), kFirstEthHostCIDR))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arc_eth1"), kSecondEthHostCIDR))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arc_wlan0"), kFirstWifiHostCIDR))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arc_wlan1"), kSecondWifiHostCIDR))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arc_wwan0"), kFirstCellHostCIDR))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_,
              ConnectVethPair(kTestPID, StrEq("arc_netns"), _, _, _, _, _, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(_, _)).WillRepeatedly(Return(true));

  auto eth0_dev = MakeShillDevice("eth0", ShillClient::Device::Type::kEthernet);
  auto eth1_dev = MakeShillDevice("eth1", ShillClient::Device::Type::kEthernet);
  auto wlan0_dev = MakeShillDevice("wlan0", ShillClient::Device::Type::kWifi);
  auto wlan1_dev = MakeShillDevice("wlan1", ShillClient::Device::Type::kWifi);
  auto wwan_dev = MakeShillDevice("wwan0", ShillClient::Device::Type::kCellular,
                                  "mbimmux0.1");
  auto svc = NewService(ArcService::ArcType::kContainer);
  svc->Start(kTestPID);
  svc->AddDevice(eth0_dev);
  svc->AddDevice(eth1_dev);
  svc->AddDevice(wlan0_dev);
  svc->AddDevice(wlan1_dev);
  svc->AddDevice(wwan_dev);
}

TEST_F(ArcServiceTest, VerifyAddrOrder) {
  EXPECT_CALL(*datapath_, NetnsAttachName(StrEq("arc_netns"), kTestPID))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arcbr0"), kArcHostCIDR))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arc_eth0"), kFirstEthHostCIDR))
      .Times(2)
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arc_wlan0"), kFirstWifiHostCIDR))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_,
              ConnectVethPair(kTestPID, StrEq("arc_netns"), _, _, _, _, _, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(_, _)).WillRepeatedly(Return(true));

  auto eth_dev = MakeShillDevice("eth0", ShillClient::Device::Type::kEthernet);
  auto wlan_dev = MakeShillDevice("wlan0", ShillClient::Device::Type::kWifi);
  auto svc = NewService(ArcService::ArcType::kContainer);
  svc->Start(kTestPID);
  svc->AddDevice(wlan_dev);
  svc->AddDevice(eth_dev);
  svc->RemoveDevice(eth_dev);
  svc->AddDevice(eth_dev);
}

TEST_F(ArcServiceTest, StableArcVmMacAddrs) {
  EXPECT_CALL(*datapath_, AddTunTap(StrEq(""), _, Eq(std::nullopt),
                                    StrEq("crosvm"), DeviceMode::kTap))
      .WillRepeatedly(Return("vmtap"));
  EXPECT_CALL(*datapath_, AddBridge(_, Property(&IPv4CIDR::prefix_length, 30)))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(_, _)).WillRepeatedly(Return(true));

  auto svc = NewService(ArcService::ArcType::kVM);
  svc->Start(kTestCID);
  auto taps = svc->GetTapDevices();
  EXPECT_EQ(taps.size(), 6);
}

// ContainerImpl

TEST_F(ArcServiceTest, ContainerImpl_Start) {
  EXPECT_CALL(*datapath_, NetnsAttachName(StrEq("arc_netns"), kTestPID))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_,
              ConnectVethPair(kTestPID, StrEq("arc_netns"), StrEq("vetharc0"),
                              StrEq("arc0"), _, kArcGuestCIDR, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(StrEq("arcbr0"), StrEq("vetharc0")))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arcbr0"), kArcHostCIDR))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, SetConntrackHelpers(true)).WillOnce(Return(true));

  auto svc = NewService(ArcService::ArcType::kContainer);
  svc->Start(kTestPID);
  EXPECT_TRUE(svc->IsStarted());

  Mock::VerifyAndClearExpectations(datapath_.get());
}

TEST_F(ArcServiceTest, ContainerImpl_FailsToCreateInterface) {
  EXPECT_CALL(*datapath_, NetnsAttachName(StrEq("arc_netns"), kTestPID))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_,
              ConnectVethPair(kTestPID, StrEq("arc_netns"), StrEq("vetharc0"),
                              StrEq("arc0"), _, kArcGuestCIDR, _, _))
      .WillOnce(Return(false));
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arcbr0"), kArcHostCIDR)).Times(0);
  EXPECT_CALL(*datapath_, RemoveBridge(_)).Times(0);
  EXPECT_CALL(*datapath_, SetConntrackHelpers(_));

  auto svc = NewService(ArcService::ArcType::kContainer);
  svc->Start(kTestPID);
  EXPECT_TRUE(svc->IsStarted());
  Mock::VerifyAndClearExpectations(datapath_.get());
}

TEST_F(ArcServiceTest, ContainerImpl_FailsToAddInterfaceToBridge) {
  EXPECT_CALL(*datapath_, NetnsAttachName(StrEq("arc_netns"), kTestPID))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_,
              ConnectVethPair(kTestPID, StrEq("arc_netns"), StrEq("vetharc0"),
                              StrEq("arc0"), _, kArcGuestCIDR, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arcbr0"), kArcHostCIDR))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(StrEq("arcbr0"), StrEq("vetharc0")))
      .WillOnce(Return(false));
  EXPECT_CALL(*datapath_, RemoveInterface(_)).Times(0);
  EXPECT_CALL(*datapath_, RemoveBridge(_)).Times(0);
  EXPECT_CALL(*datapath_, SetConntrackHelpers(true));

  auto svc = NewService(ArcService::ArcType::kContainer);
  svc->Start(kTestPID);
  EXPECT_TRUE(svc->IsStarted());
  Mock::VerifyAndClearExpectations(datapath_.get());
}

TEST_F(ArcServiceTest, ContainerImpl_OnStartDevice) {
  EXPECT_CALL(*datapath_, NetnsAttachName(StrEq("arc_netns"), kTestPID))
      .WillOnce(Return(true));
  // Expectations for arc0 setup.
  EXPECT_CALL(*datapath_,
              ConnectVethPair(kTestPID, StrEq("arc_netns"), StrEq("vetharc0"),
                              StrEq("arc0"), _, kArcGuestCIDR, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arcbr0"), kArcHostCIDR))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(StrEq("arcbr0"), StrEq("vetharc0")))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, SetConntrackHelpers(true)).WillOnce(Return(true));

  auto svc = NewService(ArcService::ArcType::kContainer);
  svc->Start(kTestPID);
  EXPECT_TRUE(svc->IsStarted());
  Mock::VerifyAndClearExpectations(datapath_.get());

  // Expectations for eth0 setup.
  EXPECT_CALL(*datapath_,
              ConnectVethPair(kTestPID, StrEq("arc_netns"), StrEq("vetheth0"),
                              StrEq("eth0"), _, kFirstEthGuestCIDR, _, true))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arc_eth0"), kFirstEthHostCIDR))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(StrEq("arc_eth0"), StrEq("vetheth0")))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_,
              StartRoutingDevice(IsShillDevice("eth0"), StrEq("arc_eth0"),
                                 TrafficSource::kArc,
                                 /*static_ipv6=*/false));
  EXPECT_CALL(*datapath_,
              AddInboundIPv4DNAT(AutoDNATTarget::kArc, IsShillDevice("eth0"),
                                 IPv4Address(100, 115, 92, 6)));

  auto eth_dev = MakeShillDevice("eth0", ShillClient::Device::Type::kEthernet);
  svc->AddDevice(eth_dev);
  Mock::VerifyAndClearExpectations(datapath_.get());
}

TEST_F(ArcServiceTest, ContainerImpl_OnStartCellularMultiplexedDevice) {
  EXPECT_CALL(*datapath_, NetnsAttachName(StrEq("arc_netns"), kTestPID))
      .WillOnce(Return(true));
  // Expectations for arc0 setup.
  EXPECT_CALL(*datapath_,
              ConnectVethPair(kTestPID, StrEq("arc_netns"), StrEq("vetharc0"),
                              StrEq("arc0"), _, kArcGuestCIDR, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arcbr0"), kArcHostCIDR))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(StrEq("arcbr0"), StrEq("vetharc0")))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, SetConntrackHelpers(true)).WillOnce(Return(true));

  auto svc = NewService(ArcService::ArcType::kContainer);
  svc->Start(kTestPID);
  EXPECT_TRUE(svc->IsStarted());
  Mock::VerifyAndClearExpectations(datapath_.get());

  // Expectations for mbimmux0.1 setup.
  EXPECT_CALL(*datapath_,
              ConnectVethPair(kTestPID, StrEq("arc_netns"), StrEq("vethwwan0"),
                              StrEq("wwan0"), _, kFirstCellGuestCIDR, _, true))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arc_wwan0"), kFirstCellHostCIDR))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(StrEq("arc_wwan0"), StrEq("vethwwan0")))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, StartRoutingDevice(
                              IsShillMultiplexedDevice("wwan0", "mbimmux0.1"),
                              StrEq("arc_wwan0"), TrafficSource::kArc,
                              /*static_ipv6=*/false));
  EXPECT_CALL(*datapath_, AddInboundIPv4DNAT(
                              AutoDNATTarget::kArc,
                              IsShillMultiplexedDevice("wwan0", "mbimmux0.1"),
                              IPv4Address(100, 115, 92, 22)));

  auto wwan_dev = MakeShillDevice("wwan0", ShillClient::Device::Type::kCellular,
                                  "mbimmux0.1");
  svc->AddDevice(wwan_dev);
  Mock::VerifyAndClearExpectations(datapath_.get());
}

TEST_F(ArcServiceTest, ContainerImpl_GetDevices) {
  EXPECT_CALL(*datapath_, NetnsAttachName(StrEq("arc_netns"), kTestPID))
      .WillOnce(Return(true));
  // Expectations for arc0 setup.
  EXPECT_CALL(*datapath_,
              ConnectVethPair(kTestPID, StrEq("arc_netns"), StrEq("vetharc0"),
                              StrEq("arc0"), _, kArcGuestCIDR, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arcbr0"), kArcHostCIDR))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(StrEq("arcbr0"), StrEq("vetharc0")))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, SetConntrackHelpers(true)).WillOnce(Return(true));

  auto eth_dev = MakeShillDevice("eth0", ShillClient::Device::Type::kEthernet);
  auto wlan_dev = MakeShillDevice("wlan0", ShillClient::Device::Type::kWifi);
  auto svc = NewService(ArcService::ArcType::kContainer);
  svc->Start(kTestPID);
  EXPECT_TRUE(svc->IsStarted());
  Mock::VerifyAndClearExpectations(datapath_.get());

  EXPECT_CALL(*datapath_, NetnsAttachName(_, _)).WillRepeatedly(Return(true));
  EXPECT_CALL(*datapath_, ConnectVethPair(_, _, _, _, _, _, _, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*datapath_, AddBridge(_, _)).WillRepeatedly(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(_, _)).WillRepeatedly(Return(true));

  svc->AddDevice(eth_dev);
  svc->AddDevice(wlan_dev);
  Mock::VerifyAndClearExpectations(datapath_.get());

  const auto devs = svc->GetDevices();
  EXPECT_EQ(devs.size(), 2);

  const auto it1 = std::find_if(devs.begin(), devs.end(),
                                [](const ArcService::ArcDevice* dev) {
                                  return dev->shill_device_ifname() == "eth0";
                                });
  ASSERT_NE(it1, devs.end());
  EXPECT_EQ((*it1)->arc_device_ifname(), "vetheth0");
  EXPECT_EQ((*it1)->bridge_ifname(), "arc_eth0");
  EXPECT_EQ((*it1)->guest_device_ifname(), "eth0");
  EXPECT_EQ((*it1)->type(), ArcService::ArcType::kContainer);

  const auto it2 = std::find_if(devs.begin(), devs.end(),
                                [](const ArcService::ArcDevice* dev) {
                                  return dev->shill_device_ifname() == "wlan0";
                                });
  ASSERT_NE(it2, devs.end());
  EXPECT_EQ((*it2)->arc_device_ifname(), "vethwlan0");
  EXPECT_EQ((*it2)->bridge_ifname(), "arc_wlan0");
  EXPECT_EQ((*it2)->guest_device_ifname(), "wlan0");
  EXPECT_EQ((*it2)->type(), ArcService::ArcType::kContainer);
}

TEST_F(ArcServiceTest, ContainerImpl_DeviceHandler) {
  EXPECT_CALL(*datapath_, NetnsAttachName(StrEq("arc_netns"), kTestPID))
      .WillOnce(Return(true));
  // Expectations for arc0 setup.
  EXPECT_CALL(*datapath_,
              ConnectVethPair(kTestPID, StrEq("arc_netns"), StrEq("vetharc0"),
                              StrEq("arc0"), _, kArcGuestCIDR, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arcbr0"), kArcHostCIDR))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(StrEq("arcbr0"), StrEq("vetharc0")))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, SetConntrackHelpers(true)).WillOnce(Return(true));

  auto eth_dev = MakeShillDevice("eth0", ShillClient::Device::Type::kEthernet);
  auto wlan_dev = MakeShillDevice("wlan0", ShillClient::Device::Type::kWifi);
  auto svc = NewService(ArcService::ArcType::kContainer);
  svc->Start(kTestPID);
  EXPECT_TRUE(svc->IsStarted());
  Mock::VerifyAndClearExpectations(datapath_.get());

  EXPECT_CALL(*datapath_, AddBridge(_, _)).WillRepeatedly(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(_, _)).WillRepeatedly(Return(true));
  EXPECT_CALL(*datapath_, ConnectVethPair(_, _, _, _, _, _, _, _))
      .WillRepeatedly(Return(true));

  svc->AddDevice(eth_dev);
  svc->AddDevice(wlan_dev);
  EXPECT_EQ(guest_device_events_.size(), 2);
  EXPECT_THAT(
      guest_device_events_,
      UnorderedElementsAre(
          Pair(StrEq("arc_eth0"), ArcService::ArcDeviceEvent::kAdded),
          Pair(StrEq("arc_wlan0"), ArcService::ArcDeviceEvent::kAdded)));
  guest_device_events_.clear();

  svc->RemoveDevice(wlan_dev);
  EXPECT_THAT(guest_device_events_,
              UnorderedElementsAre(Pair(StrEq("arc_wlan0"),
                                        ArcService::ArcDeviceEvent::kRemoved)));
  guest_device_events_.clear();

  svc->AddDevice(wlan_dev);
  EXPECT_THAT(guest_device_events_,
              UnorderedElementsAre(Pair(StrEq("arc_wlan0"),
                                        ArcService::ArcDeviceEvent::kAdded)));
  Mock::VerifyAndClearExpectations(datapath_.get());
}

TEST_F(ArcServiceTest, ContainerImpl_StartAfterDevice) {
  EXPECT_CALL(*datapath_, NetnsAttachName(StrEq("arc_netns"), kTestPID))
      .WillOnce(Return(true));
  // Expectations for arc0 setup.
  EXPECT_CALL(*datapath_,
              ConnectVethPair(kTestPID, StrEq("arc_netns"), StrEq("vetharc0"),
                              StrEq("arc0"), _, kArcGuestCIDR, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arcbr0"), kArcHostCIDR))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(StrEq("arcbr0"), StrEq("vetharc0")))
      .WillOnce(Return(true));
  // Expectations for eth0 setup.
  EXPECT_CALL(*datapath_,
              ConnectVethPair(kTestPID, StrEq("arc_netns"), StrEq("vetheth0"),
                              StrEq("eth0"), _, kFirstEthGuestCIDR, _, true))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arc_eth0"), kFirstEthHostCIDR))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(StrEq("arc_eth0"), StrEq("vetheth0")))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_,
              StartRoutingDevice(IsShillDevice("eth0"), StrEq("arc_eth0"),
                                 TrafficSource::kArc,
                                 /*static_ipv6=*/false));
  EXPECT_CALL(*datapath_,
              AddInboundIPv4DNAT(AutoDNATTarget::kArc, IsShillDevice("eth0"),
                                 IPv4Address(100, 115, 92, 6)));

  auto eth_dev = MakeShillDevice("eth0", ShillClient::Device::Type::kEthernet);
  auto svc = NewService(ArcService::ArcType::kContainer);
  svc->AddDevice(eth_dev);
  svc->Start(kTestPID);
  EXPECT_TRUE(svc->IsStarted());
  Mock::VerifyAndClearExpectations(datapath_.get());
}

TEST_F(ArcServiceTest, ContainerImpl_IPConfigurationUpdate) {
  auto svc = NewService(ArcService::ArcType::kContainer);

  // New physical device eth0.
  auto eth_dev = MakeShillDevice("eth0", ShillClient::Device::Type::kEthernet);
  eth_dev.ipconfig.ipv4_cidr =
      *net_base::IPv4CIDR::CreateFromCIDRString("192.168.1.16/24");
  eth_dev.ipconfig.ipv4_gateway = net_base::IPv4Address(192, 168, 1, 1);
  eth_dev.ipconfig.ipv4_dns_addresses = {"192.168.1.1", "8.8.8.8"};
  svc->AddDevice(eth_dev);

  // ArcService starts
  EXPECT_CALL(*datapath_, NetnsAttachName(StrEq("arc_netns"), kTestPID))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_,
              ConnectVethPair(kTestPID, StrEq("arc_netns"), StrEq("vetharc0"),
                              StrEq("arc0"), _, kArcGuestCIDR, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arcbr0"), kArcHostCIDR))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(StrEq("arcbr0"), StrEq("vetharc0")))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_,
              ConnectVethPair(kTestPID, StrEq("arc_netns"), StrEq("vetheth0"),
                              StrEq("eth0"), _, kFirstEthGuestCIDR, _, true))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arc_eth0"), kFirstEthHostCIDR))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(StrEq("arc_eth0"), StrEq("vetheth0")))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_,
              StartRoutingDevice(IsShillDevice("eth0"), StrEq("arc_eth0"),
                                 TrafficSource::kArc,
                                 /*static_ipv6=*/false));
  EXPECT_CALL(*datapath_,
              AddInboundIPv4DNAT(AutoDNATTarget::kArc, IsShillDevice("eth0"),
                                 IPv4Address(100, 115, 92, 6)));
  svc->Start(kTestPID);
  Mock::VerifyAndClearExpectations(datapath_.get());

  EXPECT_TRUE(svc->IsStarted());
  Mock::VerifyAndClearExpectations(datapath_.get());
  ASSERT_NE(shill_devices_.end(), shill_devices_.find("arc_eth0"));
  EXPECT_EQ(*net_base::IPv4CIDR::CreateFromCIDRString("192.168.1.16/24"),
            shill_devices_.find("arc_eth0")->second.ipconfig.ipv4_cidr);
  EXPECT_EQ(net_base::IPv4Address(192, 168, 1, 1),
            shill_devices_.find("arc_eth0")->second.ipconfig.ipv4_gateway);

  eth_dev.ipconfig.ipv4_cidr =
      *net_base::IPv4CIDR::CreateFromCIDRString("172.16.0.72/16");
  eth_dev.ipconfig.ipv4_gateway = net_base::IPv4Address(172, 16, 0, 1);
  eth_dev.ipconfig.ipv4_dns_addresses = {"172.17.1.1"};
  svc->UpdateDeviceIPConfig(eth_dev);

  // ArcService stops
  EXPECT_CALL(*datapath_, RemoveInterface(StrEq("vetharc0"))).Times(1);
  EXPECT_CALL(*datapath_, RemoveBridge(StrEq("arcbr0"))).Times(1);
  EXPECT_CALL(*datapath_, RemoveInterface(StrEq("vetheth0"))).Times(1);
  EXPECT_CALL(*datapath_, RemoveBridge(StrEq("arc_eth0"))).Times(1);
  EXPECT_CALL(*datapath_, SetConntrackHelpers(false)).WillOnce(Return(true));
  EXPECT_CALL(*datapath_, NetnsDeleteName(StrEq("arc_netns")))
      .WillOnce(Return(true));
  svc->Stop(kTestPID);
  ASSERT_NE(shill_devices_.end(), shill_devices_.find("arc_eth0"));
  EXPECT_EQ(*net_base::IPv4CIDR::CreateFromCIDRString("172.16.0.72/16"),
            shill_devices_.find("arc_eth0")->second.ipconfig.ipv4_cidr);
  EXPECT_EQ(net_base::IPv4Address(172, 16, 0, 1),
            shill_devices_.find("arc_eth0")->second.ipconfig.ipv4_gateway);
}

TEST_F(ArcServiceTest, ContainerImpl_Stop) {
  EXPECT_CALL(*datapath_, NetnsAttachName(StrEq("arc_netns"), kTestPID))
      .WillOnce(Return(true));
  // Expectations for arc0 setup.
  EXPECT_CALL(*datapath_,
              ConnectVethPair(kTestPID, StrEq("arc_netns"), StrEq("vetharc0"),
                              StrEq("arc0"), _, kArcGuestCIDR, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arcbr0"), kArcHostCIDR))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(StrEq("arcbr0"), StrEq("vetharc0")))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, SetConntrackHelpers(true)).WillOnce(Return(true));

  auto eth_dev = MakeShillDevice("eth0", ShillClient::Device::Type::kEthernet);
  auto svc = NewService(ArcService::ArcType::kContainer);
  svc->Start(kTestPID);
  EXPECT_TRUE(svc->IsStarted());
  Mock::VerifyAndClearExpectations(datapath_.get());

  // Expectations for eth0 setup.
  EXPECT_CALL(*datapath_,
              ConnectVethPair(kTestPID, StrEq("arc_netns"), StrEq("vetheth0"),
                              StrEq("eth0"), _, kFirstEthGuestCIDR, _, true))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arc_eth0"), kFirstEthHostCIDR))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(StrEq("arc_eth0"), StrEq("vetheth0")))
      .WillOnce(Return(true));

  svc->AddDevice(eth_dev);
  Mock::VerifyAndClearExpectations(datapath_.get());

  // Expectations for arc0 teardown.
  EXPECT_CALL(*datapath_, RemoveInterface(StrEq("vetharc0"))).Times(1);
  EXPECT_CALL(*datapath_, RemoveBridge(StrEq("arcbr0"))).Times(1);
  // Expectations for eth0 teardown.
  EXPECT_CALL(*datapath_, RemoveInterface(StrEq("vetheth0"))).Times(1);
  EXPECT_CALL(*datapath_, RemoveBridge(StrEq("arc_eth0"))).Times(1);
  // Expectations for container setup  teardown.
  EXPECT_CALL(*datapath_, SetConntrackHelpers(false)).WillOnce(Return(true));
  EXPECT_CALL(*datapath_, NetnsDeleteName(StrEq("arc_netns")))
      .WillOnce(Return(true));

  svc->Stop(kTestPID);
  EXPECT_FALSE(svc->IsStarted());
  Mock::VerifyAndClearExpectations(datapath_.get());
}

TEST_F(ArcServiceTest, ContainerImpl_OnStopDevice) {
  EXPECT_CALL(*datapath_, NetnsAttachName(StrEq("arc_netns"), kTestPID))
      .WillOnce(Return(true));
  // Expectations for arc0 setup.
  EXPECT_CALL(*datapath_,
              ConnectVethPair(kTestPID, StrEq("arc_netns"), StrEq("vetharc0"),
                              StrEq("arc0"), _, kArcGuestCIDR, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arcbr0"), kArcHostCIDR))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(StrEq("arcbr0"), StrEq("vetharc0")))
      .WillOnce(Return(true));

  auto eth_dev = MakeShillDevice("eth0", ShillClient::Device::Type::kEthernet);
  auto svc = NewService(ArcService::ArcType::kContainer);
  svc->Start(kTestPID);
  EXPECT_TRUE(svc->IsStarted());
  Mock::VerifyAndClearExpectations(datapath_.get());

  // Expectations for eth0 setup.
  EXPECT_CALL(*datapath_,
              ConnectVethPair(kTestPID, StrEq("arc_netns"), StrEq("vetheth0"),
                              StrEq("eth0"), _, kFirstEthGuestCIDR, _, true))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arc_eth0"), kFirstEthHostCIDR))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(StrEq("arc_eth0"), StrEq("vetheth0")))
      .WillOnce(Return(true));

  svc->AddDevice(eth_dev);
  Mock::VerifyAndClearExpectations(datapath_.get());

  // Expectations for eth0 teardown.
  EXPECT_CALL(*datapath_, RemoveInterface(StrEq("vetheth0"))).Times(1);
  EXPECT_CALL(*datapath_, StopRoutingDevice(StrEq("arc_eth0")));
  EXPECT_CALL(*datapath_,
              RemoveInboundIPv4DNAT(AutoDNATTarget::kArc, IsShillDevice("eth0"),
                                    IPv4Address(100, 115, 92, 6)));
  EXPECT_CALL(*datapath_, RemoveBridge(StrEq("arc_eth0"))).Times(1);

  svc->RemoveDevice(eth_dev);
  Mock::VerifyAndClearExpectations(datapath_.get());
}

TEST_F(ArcServiceTest, ContainerImpl_Restart) {
  EXPECT_CALL(*datapath_, NetnsAttachName(StrEq("arc_netns"), kTestPID))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_,
              ConnectVethPair(kTestPID, StrEq("arc_netns"), StrEq("vetharc0"),
                              StrEq("arc0"), _, kArcGuestCIDR, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arcbr0"), kArcHostCIDR))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(StrEq("arcbr0"), StrEq("vetharc0")))
      .WillOnce(Return(true));

  auto eth_dev = MakeShillDevice("eth0", ShillClient::Device::Type::kEthernet);
  auto svc = NewService(ArcService::ArcType::kContainer);
  svc->Start(kTestPID);
  EXPECT_TRUE(svc->IsStarted());
  Mock::VerifyAndClearExpectations(datapath_.get());

  // Expectations for eth0 setup.
  EXPECT_CALL(*datapath_,
              ConnectVethPair(kTestPID, StrEq("arc_netns"), StrEq("vetheth0"),
                              StrEq("eth0"), _, kFirstEthGuestCIDR, _, true))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arc_eth0"), kFirstEthHostCIDR))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(StrEq("arc_eth0"), StrEq("vetheth0")))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_,
              StartRoutingDevice(IsShillDevice("eth0"), StrEq("arc_eth0"),
                                 TrafficSource::kArc,
                                 /*static_ipv6=*/false));
  EXPECT_CALL(*datapath_,
              AddInboundIPv4DNAT(AutoDNATTarget::kArc, IsShillDevice("eth0"),
                                 IPv4Address(100, 115, 92, 6)));
  svc->AddDevice(eth_dev);
  Mock::VerifyAndClearExpectations(datapath_.get());

  // Expectations for arc0, eth0, and arc netns teardown.
  EXPECT_CALL(*datapath_, RemoveInterface(StrEq("vetharc0"))).Times(1);
  EXPECT_CALL(*datapath_, RemoveBridge(StrEq("arcbr0"))).Times(1);
  EXPECT_CALL(*datapath_, RemoveInterface(StrEq("vetheth0"))).Times(1);
  EXPECT_CALL(*datapath_, RemoveBridge(StrEq("arc_eth0"))).Times(1);
  EXPECT_CALL(*datapath_, SetConntrackHelpers(false)).WillOnce(Return(true));
  EXPECT_CALL(*datapath_, NetnsDeleteName(StrEq("arc_netns")))
      .WillOnce(Return(true));
  svc->Stop(kTestPID);
  EXPECT_FALSE(svc->IsStarted());
  Mock::VerifyAndClearExpectations(datapath_.get());

  // Expectations for arc0, eth0, and arc netns setup on restart.
  EXPECT_CALL(*datapath_, NetnsAttachName(StrEq("arc_netns"), kTestPID))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_,
              ConnectVethPair(kTestPID, StrEq("arc_netns"), StrEq("vetharc0"),
                              StrEq("arc0"), _, kArcGuestCIDR, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arcbr0"), kArcHostCIDR))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(StrEq("arcbr0"), StrEq("vetharc0")))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_,
              ConnectVethPair(kTestPID, StrEq("arc_netns"), StrEq("vetheth0"),
                              StrEq("eth0"), _, kFirstEthGuestCIDR, _, true))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arc_eth0"), kFirstEthHostCIDR))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(StrEq("arc_eth0"), StrEq("vetheth0")))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_,
              StartRoutingDevice(IsShillDevice("eth0"), StrEq("arc_eth0"),
                                 TrafficSource::kArc,
                                 /*static_ipv6=*/false));
  EXPECT_CALL(*datapath_,
              AddInboundIPv4DNAT(AutoDNATTarget::kArc, IsShillDevice("eth0"),
                                 IPv4Address(100, 115, 92, 6)));
  svc->Start(kTestPID);
  EXPECT_TRUE(svc->IsStarted());
  Mock::VerifyAndClearExpectations(datapath_.get());
}

// VM Impl

TEST_F(ArcServiceTest, VmImpl_Start) {
  // Expectations for tap devices pre-creation.
  EXPECT_CALL(*datapath_, AddTunTap(StrEq(""), _, Eq(std::nullopt),
                                    StrEq("crosvm"), DeviceMode::kTap))
      .WillOnce(Return("vmtap0"))
      .WillOnce(Return("vmtap1"))
      .WillOnce(Return("vmtap2"))
      .WillOnce(Return("vmtap3"))
      .WillOnce(Return("vmtap4"))
      .WillOnce(Return("vmtap5"));
  // Expectations for "arc0" setup.
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arcbr0"), kArcHostCIDR))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(StrEq("arcbr0"), StrEq("vmtap0")))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, SetConntrackHelpers(true)).WillOnce(Return(true));

  auto svc = NewService(ArcService::ArcType::kVM);
  svc->Start(kTestCID);
  EXPECT_TRUE(svc->IsStarted());
  Mock::VerifyAndClearExpectations(datapath_.get());
}

TEST_F(ArcServiceTest, VmImpl_StartEthernetDevice) {
  // Expectations for tap devices pre-creation.
  EXPECT_CALL(*datapath_, AddTunTap(StrEq(""), _, Eq(std::nullopt),
                                    StrEq("crosvm"), DeviceMode::kTap))
      .WillOnce(Return("vmtap0"))
      .WillOnce(Return("vmtap1"))
      .WillOnce(Return("vmtap2"))
      .WillOnce(Return("vmtap3"))
      .WillOnce(Return("vmtap4"))
      .WillOnce(Return("vmtap5"));
  // Expectations for "arc0" setup.
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arcbr0"), kArcHostCIDR))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(StrEq("arcbr0"), StrEq("vmtap0")))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, SetConntrackHelpers(true)).WillOnce(Return(true));

  auto eth_dev = MakeShillDevice("eth0", ShillClient::Device::Type::kEthernet);
  auto svc = NewService(ArcService::ArcType::kVM);
  svc->Start(kTestCID);
  EXPECT_TRUE(svc->IsStarted());
  Mock::VerifyAndClearExpectations(datapath_.get());

  // Expectations for eth0 setup.
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arc_eth0"), kFirstEthHostCIDR))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(StrEq("arc_eth0"), StrEq("vmtap1")))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_,
              StartRoutingDevice(IsShillDevice("eth0"), StrEq("arc_eth0"),
                                 TrafficSource::kArc,
                                 /*static_ipv6=*/false));
  EXPECT_CALL(*datapath_,
              AddInboundIPv4DNAT(AutoDNATTarget::kArc, IsShillDevice("eth0"),
                                 IPv4Address(100, 115, 92, 6)));

  svc->AddDevice(eth_dev);
  Mock::VerifyAndClearExpectations(datapath_.get());
}

TEST_F(ArcServiceTest, VmImpl_StartCellularMultiplexedDevice) {
  // Expectations for tap devices pre-creation.
  EXPECT_CALL(*datapath_, AddTunTap(StrEq(""), _, Eq(std::nullopt),
                                    StrEq("crosvm"), DeviceMode::kTap))
      .WillOnce(Return("vmtap0"))
      .WillOnce(Return("vmtap1"))
      .WillOnce(Return("vmtap2"))
      .WillOnce(Return("vmtap3"))
      .WillOnce(Return("vmtap4"))
      .WillOnce(Return("vmtap5"));
  // Expectations for "arc0" setup.
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arcbr0"), kArcHostCIDR))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(StrEq("arcbr0"), StrEq("vmtap0")))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, SetConntrackHelpers(true)).WillOnce(Return(true));

  auto wwan_dev = MakeShillDevice("wwan0", ShillClient::Device::Type::kCellular,
                                  "mbimmux0.1");
  auto svc = NewService(ArcService::ArcType::kVM);
  svc->Start(kTestCID);
  EXPECT_TRUE(svc->IsStarted());
  Mock::VerifyAndClearExpectations(datapath_.get());

  // Expectations for mbimmux0.1  setup.
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arc_wwan0"), kFirstCellHostCIDR))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(StrEq("arc_wwan0"), StrEq("vmtap5")))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, StartRoutingDevice(
                              IsShillMultiplexedDevice("wwan0", "mbimmux0.1"),
                              StrEq("arc_wwan0"), TrafficSource::kArc,
                              /*static_ipv6=*/false));
  EXPECT_CALL(*datapath_, AddInboundIPv4DNAT(
                              AutoDNATTarget::kArc,
                              IsShillMultiplexedDevice("wwan0", "mbimmux0.1"),
                              IPv4Address(100, 115, 92, 22)));

  svc->AddDevice(wwan_dev);
  Mock::VerifyAndClearExpectations(datapath_.get());
}
TEST_F(ArcServiceTest, VmImpl_StartMultipleDevices) {
  // Expectations for tap devices pre-creation.
  EXPECT_CALL(*datapath_, AddTunTap(StrEq(""), _, Eq(std::nullopt),
                                    StrEq("crosvm"), DeviceMode::kTap))
      .WillOnce(Return("vmtap0"))
      .WillOnce(Return("vmtap1"))
      .WillOnce(Return("vmtap2"))
      .WillOnce(Return("vmtap3"))
      .WillOnce(Return("vmtap4"))
      .WillOnce(Return("vmtap5"));
  // Expectations for "arc0" setup.
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arcbr0"), kArcHostCIDR))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(StrEq("arcbr0"), StrEq("vmtap0")))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, SetConntrackHelpers(true)).WillOnce(Return(true));

  auto eth0_dev = MakeShillDevice("eth0", ShillClient::Device::Type::kEthernet);
  auto eth1_dev = MakeShillDevice("eth1", ShillClient::Device::Type::kEthernet);
  auto wlan_dev = MakeShillDevice("wlan0", ShillClient::Device::Type::kWifi);
  auto svc = NewService(ArcService::ArcType::kVM);
  svc->Start(kTestCID);
  EXPECT_TRUE(svc->IsStarted());
  Mock::VerifyAndClearExpectations(datapath_.get());

  // Expectations for eth0 setup.
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arc_eth0"), kFirstEthHostCIDR))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(StrEq("arc_eth0"), StrEq("vmtap1")))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_,
              StartRoutingDevice(IsShillDevice("eth0"), StrEq("arc_eth0"),
                                 TrafficSource::kArc,
                                 /*static_ipv6=*/false));
  EXPECT_CALL(*datapath_,
              AddInboundIPv4DNAT(AutoDNATTarget::kArc, IsShillDevice("eth0"),
                                 IPv4Address(100, 115, 92, 6)));

  svc->AddDevice(eth0_dev);
  Mock::VerifyAndClearExpectations(datapath_.get());

  // Expectations for wlan0 setup.
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arc_wlan0"), kFirstWifiHostCIDR))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(StrEq("arc_wlan0"), StrEq("vmtap3")))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_,
              StartRoutingDevice(IsShillDevice("wlan0"), StrEq("arc_wlan0"),
                                 TrafficSource::kArc,
                                 /*static_ipv6=*/false));
  EXPECT_CALL(*datapath_,
              AddInboundIPv4DNAT(AutoDNATTarget::kArc, IsShillDevice("wlan0"),
                                 IPv4Address(100, 115, 92, 14)));

  svc->AddDevice(wlan_dev);
  Mock::VerifyAndClearExpectations(datapath_.get());

  // Expectations for eth1 setup.
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arc_eth1"), kSecondEthHostCIDR))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(StrEq("arc_eth1"), StrEq("vmtap2")))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_,
              StartRoutingDevice(IsShillDevice("eth1"), StrEq("arc_eth1"),
                                 TrafficSource::kArc,
                                 /*static_ipv6=*/false));
  EXPECT_CALL(*datapath_,
              AddInboundIPv4DNAT(AutoDNATTarget::kArc, IsShillDevice("eth1"),
                                 IPv4Address(100, 115, 92, 10)));

  svc->AddDevice(eth1_dev);
  Mock::VerifyAndClearExpectations(datapath_.get());
}

TEST_F(ArcServiceTest, VmImpl_Stop) {
  // Expectations for tap devices pre-creation.
  EXPECT_CALL(*datapath_, AddTunTap(StrEq(""), _, Eq(std::nullopt),
                                    StrEq("crosvm"), DeviceMode::kTap))
      .WillOnce(Return("vmtap0"))
      .WillOnce(Return("vmtap1"))
      .WillOnce(Return("vmtap2"))
      .WillOnce(Return("vmtap3"))
      .WillOnce(Return("vmtap4"))
      .WillOnce(Return("vmtap5"));
  // Expectations for "arc0" setup.
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arcbr0"), kArcHostCIDR))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(StrEq("arcbr0"), StrEq("vmtap0")))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, SetConntrackHelpers(true)).WillOnce(Return(true));

  auto svc = NewService(ArcService::ArcType::kVM);
  svc->Start(kTestCID);
  EXPECT_TRUE(svc->IsStarted());
  Mock::VerifyAndClearExpectations(datapath_.get());

  // Expectations for "arc0" teardown.
  EXPECT_CALL(*datapath_, RemoveBridge(StrEq("arcbr0"))).Times(1);
  EXPECT_CALL(*datapath_, RemoveInterface(StrEq("vetharc0"))).Times(0);
  // Expectations for tap devices teardown
  EXPECT_CALL(*datapath_, RemoveInterface(StrEq("vmtap0")));
  EXPECT_CALL(*datapath_, RemoveInterface(StrEq("vmtap1")));
  EXPECT_CALL(*datapath_, RemoveInterface(StrEq("vmtap2")));
  EXPECT_CALL(*datapath_, RemoveInterface(StrEq("vmtap3")));
  EXPECT_CALL(*datapath_, RemoveInterface(StrEq("vmtap4")));
  EXPECT_CALL(*datapath_, RemoveInterface(StrEq("vmtap5")));
  EXPECT_CALL(*datapath_, SetConntrackHelpers(false)).WillOnce(Return(true));

  svc->Stop(kTestCID);
  EXPECT_FALSE(svc->IsStarted());
  Mock::VerifyAndClearExpectations(datapath_.get());
}

TEST_F(ArcServiceTest, VmImpl_Restart) {
  // Expectations for tap devices pre-creation.
  EXPECT_CALL(*datapath_, AddTunTap(StrEq(""), _, Eq(std::nullopt),
                                    StrEq("crosvm"), DeviceMode::kTap))
      .WillOnce(Return("vmtap0"))
      .WillOnce(Return("vmtap1"))
      .WillOnce(Return("vmtap2"))
      .WillOnce(Return("vmtap3"))
      .WillOnce(Return("vmtap4"))
      .WillOnce(Return("vmtap5"));
  // Expectations for "arc0" setup.
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arcbr0"), kArcHostCIDR))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(StrEq("arcbr0"), StrEq("vmtap0")))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, SetConntrackHelpers(true)).WillOnce(Return(true));

  auto eth_dev = MakeShillDevice("eth0", ShillClient::Device::Type::kEthernet);
  auto svc = NewService(ArcService::ArcType::kVM);
  svc->Start(kTestCID);
  EXPECT_TRUE(svc->IsStarted());
  Mock::VerifyAndClearExpectations(datapath_.get());

  // Expectations for eth0 setup.
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arc_eth0"), kFirstEthHostCIDR))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(StrEq("arc_eth0"), StrEq("vmtap1")))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_,
              StartRoutingDevice(IsShillDevice("eth0"), StrEq("arc_eth0"),
                                 TrafficSource::kArc,
                                 /*static_ipv6=*/false));
  EXPECT_CALL(*datapath_,
              AddInboundIPv4DNAT(AutoDNATTarget::kArc, IsShillDevice("eth0"),
                                 IPv4Address(100, 115, 92, 6)));
  svc->AddDevice(eth_dev);
  Mock::VerifyAndClearExpectations(datapath_.get());

  // Expectations for arc0, eth0, and tap devices teardown.
  EXPECT_CALL(*datapath_, RemoveBridge(StrEq("arcbr0"))).Times(1);
  EXPECT_CALL(*datapath_, RemoveInterface(StrEq("vetharc0"))).Times(0);
  EXPECT_CALL(*datapath_, RemoveInterface(StrEq("vmtap0")));
  EXPECT_CALL(*datapath_, RemoveInterface(StrEq("vmtap1")));
  EXPECT_CALL(*datapath_, RemoveInterface(StrEq("vmtap2")));
  EXPECT_CALL(*datapath_, RemoveInterface(StrEq("vmtap3")));
  EXPECT_CALL(*datapath_, RemoveInterface(StrEq("vmtap4")));
  EXPECT_CALL(*datapath_, RemoveInterface(StrEq("vmtap5")));
  EXPECT_CALL(*datapath_, SetConntrackHelpers(false)).WillOnce(Return(true));
  EXPECT_CALL(*datapath_, StopRoutingDevice(StrEq("arc_eth0")));
  EXPECT_CALL(*datapath_,
              RemoveInboundIPv4DNAT(AutoDNATTarget::kArc, IsShillDevice("eth0"),
                                    IPv4Address(100, 115, 92, 6)));
  EXPECT_CALL(*datapath_, RemoveBridge(StrEq("arc_eth0")));
  svc->Stop(kTestCID);
  EXPECT_FALSE(svc->IsStarted());
  Mock::VerifyAndClearExpectations(datapath_.get());

  // Expectations for arc0, eth0, and tap device pre-creation on restart.
  EXPECT_CALL(*datapath_, AddTunTap(StrEq(""), _, Eq(std::nullopt),
                                    StrEq("crosvm"), DeviceMode::kTap))
      .WillOnce(Return("vmtap0"))
      .WillOnce(Return("vmtap1"))
      .WillOnce(Return("vmtap2"))
      .WillOnce(Return("vmtap3"))
      .WillOnce(Return("vmtap4"))
      .WillOnce(Return("vmtap5"));
  // Expectations for "arc0" setup.
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arcbr0"), kArcHostCIDR))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(StrEq("arcbr0"), StrEq("vmtap0")))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, SetConntrackHelpers(true)).WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arc_eth0"), kFirstEthHostCIDR))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(StrEq("arc_eth0"), StrEq("vmtap1")))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_,
              StartRoutingDevice(IsShillDevice("eth0"), StrEq("arc_eth0"),
                                 TrafficSource::kArc,
                                 /*static_ipv6=*/false));
  EXPECT_CALL(*datapath_,
              AddInboundIPv4DNAT(AutoDNATTarget::kArc, IsShillDevice("eth0"),
                                 IPv4Address(100, 115, 92, 6)));
  svc->Start(kTestCID);
  EXPECT_TRUE(svc->IsStarted());
  Mock::VerifyAndClearExpectations(datapath_.get());
}

TEST_F(ArcServiceTest, VmImpl_StopDevice) {
  // Expectations for tap devices pre-creation.
  EXPECT_CALL(*datapath_, AddTunTap(StrEq(""), _, Eq(std::nullopt),
                                    StrEq("crosvm"), DeviceMode::kTap))
      .WillOnce(Return("vmtap0"))
      .WillOnce(Return("vmtap1"))
      .WillOnce(Return("vmtap2"))
      .WillOnce(Return("vmtap3"))
      .WillOnce(Return("vmtap4"))
      .WillOnce(Return("vmtap5"));
  // Expectations for "arc0" setup.
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arcbr0"), kArcHostCIDR))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(StrEq("arcbr0"), StrEq("vmtap0")))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, SetConntrackHelpers(true)).WillOnce(Return(true));

  auto eth_dev = MakeShillDevice("eth0", ShillClient::Device::Type::kEthernet);
  auto svc = NewService(ArcService::ArcType::kVM);
  svc->Start(kTestCID);
  EXPECT_TRUE(svc->IsStarted());
  Mock::VerifyAndClearExpectations(datapath_.get());

  // Expectations for eth0 setup.
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arc_eth0"), kFirstEthHostCIDR))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(StrEq("arc_eth0"), StrEq("vmtap1")))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_,
              StartRoutingDevice(IsShillDevice("eth0"), StrEq("arc_eth0"),
                                 TrafficSource::kArc,
                                 /*static_ipv6=*/false));
  EXPECT_CALL(*datapath_,
              AddInboundIPv4DNAT(AutoDNATTarget::kArc, IsShillDevice("eth0"),
                                 IPv4Address(100, 115, 92, 6)));

  svc->AddDevice(eth_dev);
  Mock::VerifyAndClearExpectations(datapath_.get());

  // Expectations for eth0 teardown.
  EXPECT_CALL(*datapath_, StopRoutingDevice(StrEq("arc_eth0")));
  EXPECT_CALL(*datapath_,
              RemoveInboundIPv4DNAT(AutoDNATTarget::kArc, IsShillDevice("eth0"),
                                    IPv4Address(100, 115, 92, 6)));
  EXPECT_CALL(*datapath_, RemoveBridge(StrEq("arc_eth0")));

  svc->RemoveDevice(eth_dev);
  Mock::VerifyAndClearExpectations(datapath_.get());
}

TEST_F(ArcServiceTest, VmImpl_GetDevices) {
  // Expectations for tap devices pre-creation.
  EXPECT_CALL(*datapath_, AddTunTap(StrEq(""), _, Eq(std::nullopt),
                                    StrEq("crosvm"), DeviceMode::kTap))
      .WillOnce(Return("vmtap0"))
      .WillOnce(Return("vmtap1"))
      .WillOnce(Return("vmtap2"))
      .WillOnce(Return("vmtap3"))
      .WillOnce(Return("vmtap4"))
      .WillOnce(Return("vmtap5"));
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arcbr0"), kArcHostCIDR))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(StrEq("arcbr0"), StrEq("vmtap0")))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, SetConntrackHelpers(true)).WillOnce(Return(true));

  auto eth0_dev = MakeShillDevice("eth0", ShillClient::Device::Type::kEthernet);
  auto eth1_dev = MakeShillDevice("eth1", ShillClient::Device::Type::kEthernet);
  auto wlan0_dev = MakeShillDevice("wlan0", ShillClient::Device::Type::kWifi);
  auto svc = NewService(ArcService::ArcType::kVM);
  svc->Start(kTestCID);
  Mock::VerifyAndClearExpectations(datapath_.get());

  EXPECT_CALL(*datapath_, AddBridge(_, _)).WillRepeatedly(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(_, _)).WillRepeatedly(Return(true));

  svc->AddDevice(eth0_dev);
  svc->AddDevice(eth1_dev);
  svc->AddDevice(wlan0_dev);
  Mock::VerifyAndClearExpectations(datapath_.get());

  const auto devs = svc->GetDevices();
  EXPECT_EQ(devs.size(), 3);

  const auto it1 = std::find_if(devs.begin(), devs.end(),
                                [](const ArcService::ArcDevice* dev) {
                                  return dev->shill_device_ifname() == "eth0";
                                });
  ASSERT_NE(it1, devs.end());
  EXPECT_EQ((*it1)->arc_device_ifname(), "vmtap1");
  EXPECT_EQ((*it1)->bridge_ifname(), "arc_eth0");
  EXPECT_EQ((*it1)->guest_device_ifname(), "eth1");
  EXPECT_EQ((*it1)->type(), ArcService::ArcType::kVM);

  const auto it2 = std::find_if(devs.begin(), devs.end(),
                                [](const ArcService::ArcDevice* dev) {
                                  return dev->shill_device_ifname() == "wlan0";
                                });
  ASSERT_NE(it2, devs.end());
  EXPECT_EQ((*it2)->arc_device_ifname(), "vmtap3");
  EXPECT_EQ((*it2)->bridge_ifname(), "arc_wlan0");
  EXPECT_EQ((*it2)->guest_device_ifname(), "eth3");
  EXPECT_EQ((*it2)->type(), ArcService::ArcType::kVM);

  const auto it3 = std::find_if(devs.begin(), devs.end(),
                                [](const ArcService::ArcDevice* dev) {
                                  return dev->shill_device_ifname() == "eth1";
                                });
  ASSERT_NE(it3, devs.end());
  EXPECT_EQ((*it3)->arc_device_ifname(), "vmtap2");
  EXPECT_EQ((*it3)->bridge_ifname(), "arc_eth1");
  EXPECT_EQ((*it3)->guest_device_ifname(), "eth2");
  EXPECT_EQ((*it3)->type(), ArcService::ArcType::kVM);
}

TEST_F(ArcServiceTest, VmImpl_DeviceHandler) {
  // Expectations for tap devices pre-creation.
  EXPECT_CALL(*datapath_, AddTunTap(StrEq(""), _, Eq(std::nullopt),
                                    StrEq("crosvm"), DeviceMode::kTap))
      .WillOnce(Return("vmtap0"))
      .WillOnce(Return("vmtap1"))
      .WillOnce(Return("vmtap2"))
      .WillOnce(Return("vmtap3"))
      .WillOnce(Return("vmtap4"))
      .WillOnce(Return("vmtap5"));
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arcbr0"), kArcHostCIDR))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(StrEq("arcbr0"), StrEq("vmtap0")))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, SetConntrackHelpers(true)).WillOnce(Return(true));

  auto eth_dev = MakeShillDevice("eth0", ShillClient::Device::Type::kEthernet);
  auto wlan_dev = MakeShillDevice("wlan0", ShillClient::Device::Type::kWifi);
  auto svc = NewService(ArcService::ArcType::kVM);
  svc->Start(kTestCID);
  EXPECT_TRUE(svc->IsStarted());
  Mock::VerifyAndClearExpectations(datapath_.get());

  EXPECT_CALL(*datapath_, AddBridge(_, _)).WillRepeatedly(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(_, _)).WillRepeatedly(Return(true));

  svc->AddDevice(eth_dev);
  svc->AddDevice(wlan_dev);
  EXPECT_EQ(guest_device_events_.size(), 2);
  EXPECT_THAT(
      guest_device_events_,
      UnorderedElementsAre(
          Pair(StrEq("arc_eth0"), ArcService::ArcDeviceEvent::kAdded),
          Pair(StrEq("arc_wlan0"), ArcService::ArcDeviceEvent::kAdded)));
  guest_device_events_.clear();

  svc->RemoveDevice(wlan_dev);
  EXPECT_THAT(guest_device_events_,
              UnorderedElementsAre(Pair(StrEq("arc_wlan0"),
                                        ArcService::ArcDeviceEvent::kRemoved)));
  guest_device_events_.clear();

  svc->AddDevice(wlan_dev);
  EXPECT_THAT(guest_device_events_,
              UnorderedElementsAre(Pair(StrEq("arc_wlan0"),
                                        ArcService::ArcDeviceEvent::kAdded)));
  Mock::VerifyAndClearExpectations(datapath_.get());
}

TEST_F(ArcServiceTest, VmImpl_ArcvmInterfaceMapping) {
  // Expectations for tap devices pre-creation.
  EXPECT_CALL(*datapath_, AddTunTap(StrEq(""), _, Eq(std::nullopt),
                                    StrEq("crosvm"), DeviceMode::kTap))
      .WillOnce(Return("vmtap2"))
      .WillOnce(Return("vmtap3"))
      .WillOnce(Return("vmtap4"))
      .WillOnce(Return("vmtap5"))
      .WillOnce(Return("vmtap6"))
      .WillOnce(Return("vmtap8"));

  auto svc = NewService(ArcService::ArcType::kVM);
  svc->Start(kTestCID);

  std::map<std::string, std::string> arcvm_guest_ifnames = {
      {"vmtap2", "eth0"}, {"vmtap3", "eth1"}, {"vmtap4", "eth2"},
      {"vmtap5", "eth3"}, {"vmtap6", "eth4"}, {"vmtap8", "eth5"},
  };

  for (const auto& [tap, arcvm_ifname] : arcvm_guest_ifnames) {
    auto it = svc->arcvm_guest_ifnames_.find(tap);
    EXPECT_TRUE(it != svc->arcvm_guest_ifnames_.end());
    EXPECT_EQ(it->second, arcvm_ifname);
  }
}

TEST_F(ArcServiceTest, ArcVethHostName) {
  static struct {
    std::string shill_device_interface_property;
    std::string expected_veth_ifname;
  } test_cases[] = {
      {"eth0", "vetheth0"},
      {"rmnet0", "vethrmnet0"},
      {"rmnet_data0", "vethrmnet_data0"},
      {"ifnamsiz_ifnam0", "vethifnamsiz_i0"},
      {"exceeds_ifnamesiz_checkanyway", "vethexceeds_ify"},
  };

  for (const auto& tc : test_cases) {
    ShillClient::Device device;
    device.shill_device_interface_property = tc.shill_device_interface_property;
    auto ifname = ArcService::ArcVethHostName(device);
    EXPECT_EQ(tc.expected_veth_ifname, ifname);
    EXPECT_LT(ifname.length(), IFNAMSIZ);
  }
}

TEST_F(ArcServiceTest, ArcBridgeName) {
  static struct {
    std::string shill_device_interface_property;
    std::string expected_bridge_name;
  } test_cases[] = {
      {"eth0", "arc_eth0"},
      {"rmnet0", "arc_rmnet0"},
      {"rmnet_data0", "arc_rmnet_data0"},
      {"ifnamsiz_ifnam0", "arc_ifnamsiz_i0"},
      {"ifnamesize0", "arc_ifnamesize0"},
      {"if_namesize0", "arc_if_namesiz0"},
      {"exceeds_ifnamesiz_checkanyway", "arc_exceeds_ify"},
  };

  for (const auto& tc : test_cases) {
    ShillClient::Device device;
    device.shill_device_interface_property = tc.shill_device_interface_property;
    auto bridge = ArcService::ArcBridgeName(device);
    EXPECT_EQ(tc.expected_bridge_name, bridge);
    EXPECT_LT(bridge.length(), IFNAMSIZ);
  }
}

}  // namespace patchpanel
