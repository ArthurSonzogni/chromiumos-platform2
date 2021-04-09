// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/arc_service.h"

#include <net/if.h>

#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/callback_helpers.h>
#include <dbus/mock_bus.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "patchpanel/address_manager.h"
#include "patchpanel/fake_shill_client.h"
#include "patchpanel/mock_datapath.h"
#include "patchpanel/net_util.h"

using testing::_;
using testing::AnyNumber;
using testing::Eq;
using testing::Invoke;
using testing::Mock;
using testing::Pair;
using testing::Pointee;
using testing::Return;
using testing::ReturnRef;
using testing::StrEq;
using testing::UnorderedElementsAre;

namespace patchpanel {
namespace {
constexpr pid_t kTestPID = -2;
constexpr uint32_t kTestCID = 2;
constexpr uint32_t kArcHostIP = Ipv4Addr(100, 115, 92, 1);
constexpr uint32_t kArcGuestIP = Ipv4Addr(100, 115, 92, 2);
constexpr uint32_t kFirstEthHostIP = Ipv4Addr(100, 115, 92, 5);
constexpr uint32_t kFirstEthGuestIP = Ipv4Addr(100, 115, 92, 6);
constexpr uint32_t kSecondEthHostIP = Ipv4Addr(100, 115, 92, 9);
constexpr uint32_t kFirstWifiHostIP = Ipv4Addr(100, 115, 92, 13);
constexpr uint32_t kSecondWifiHostIP = Ipv4Addr(100, 115, 92, 17);
constexpr uint32_t kFirstCellHostIP = Ipv4Addr(100, 115, 92, 21);
constexpr MacAddress kArcVmArc0MacAddr = {0x42, 0x37, 0x05, 0x13, 0x17, 0x01};

class MockShillClient : public ShillClient {
 public:
  explicit MockShillClient(scoped_refptr<dbus::MockBus> bus)
      : ShillClient(bus) {}
  ~MockShillClient() = default;

  MOCK_METHOD2(GetDeviceProperties,
               bool(const std::string& ifname, ShillClient::Device* device));
};

}  // namespace

class ArcServiceTest : public testing::Test {
 public:
  ArcServiceTest() : testing::Test() {}

 protected:
  void SetUp() override {
    datapath_ = std::make_unique<MockDatapath>();
    shill_client_ = std::make_unique<MockShillClient>(shill_helper_.mock_bus());
    addr_mgr_ = std::make_unique<AddressManager>();
    guest_devices_.clear();
  }

  std::unique_ptr<ArcService> NewService(GuestMessage::GuestType guest) {
    return std::make_unique<ArcService>(
        shill_client_.get(), datapath_.get(), addr_mgr_.get(), guest,
        base::BindRepeating(&ArcServiceTest::DeviceHandler,
                            base::Unretained(this)));
  }

  void DeviceHandler(const Device& device,
                     Device::ChangeEvent event,
                     GuestMessage::GuestType guest_type) {
    guest_devices_[device.host_ifname()] = event;
  }

  void ExpectGetDeviceProperties(const std::string& iface,
                                 ShillClient::Device::Type type) {
    EXPECT_CALL(*shill_client_, GetDeviceProperties(StrEq(iface), _))
        .WillRepeatedly(
            Invoke([type](const std::string& _, ShillClient::Device* device) {
              device->type = type;
              return true;
            }));
  }

  FakeShillClientHelper shill_helper_;
  std::unique_ptr<MockShillClient> shill_client_;
  std::unique_ptr<AddressManager> addr_mgr_;
  std::unique_ptr<MockDatapath> datapath_;
  std::map<std::string, Device::ChangeEvent> guest_devices_;
};

TEST_F(ArcServiceTest, NotStarted_AddDevice) {
  ExpectGetDeviceProperties("eth0", ShillClient::Device::Type::kEthernet);
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arc_eth0"), _, _)).Times(0);
  EXPECT_CALL(*datapath_, StartRoutingDevice(StrEq("eth0"), StrEq("arc_eth0"),
                                             _, _, false, _))
      .Times(0);

  auto svc = NewService(GuestMessage::ARC);
  svc->OnDevicesChanged({"eth0"}, {});
  EXPECT_TRUE(svc->devices_.find("eth0") == svc->devices_.end());
  EXPECT_FALSE(svc->shill_devices_.find("eth0") == svc->shill_devices_.end());
}

TEST_F(ArcServiceTest, NotStarted_AddRemoveDevice) {
  ExpectGetDeviceProperties("eth0", ShillClient::Device::Type::kEthernet);
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arc_eth0"), _, _)).Times(0);
  EXPECT_CALL(*datapath_, StartRoutingDevice(StrEq("eth0"), StrEq("arc_eth0"),
                                             _, _, false, _))
      .Times(0);
  EXPECT_CALL(*datapath_,
              StopRoutingDevice(StrEq("eth0"), StrEq("arc_eth0"), _, _, false))
      .Times(0);
  EXPECT_CALL(*datapath_, RemoveBridge(StrEq("arc_eth0"))).Times(0);

  auto svc = NewService(GuestMessage::ARC);
  svc->OnDevicesChanged({"eth0"}, {});
  svc->OnDevicesChanged({}, {"eth0"});
  EXPECT_TRUE(svc->devices_.find("eth0") == svc->devices_.end());
  EXPECT_TRUE(svc->shill_devices_.find("eth0") == svc->shill_devices_.end());
}

TEST_F(ArcServiceTest, VerifyAddrConfigs) {
  ExpectGetDeviceProperties("eth0", ShillClient::Device::Type::kEthernet);
  ExpectGetDeviceProperties("eth1", ShillClient::Device::Type::kEthernet);
  ExpectGetDeviceProperties("wlan0", ShillClient::Device::Type::kWifi);
  ExpectGetDeviceProperties("wlan1", ShillClient::Device::Type::kWifi);
  ExpectGetDeviceProperties("wwan0", ShillClient::Device::Type::kCellular);
  EXPECT_CALL(*datapath_, NetnsAttachName(StrEq("arc_netns"), kTestPID))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arcbr0"), kArcHostIP, 30))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arc_eth0"), kFirstEthHostIP, 30))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arc_eth1"), kSecondEthHostIP, 30))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arc_wlan0"), kFirstWifiHostIP, 30))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arc_wlan1"), kSecondWifiHostIP, 30))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arc_wwan0"), kFirstCellHostIP, 30))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_,
              ConnectVethPair(kTestPID, StrEq("arc_netns"), _, _, _, _, _, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(_, _)).WillRepeatedly(Return(true));

  auto svc = NewService(GuestMessage::ARC);
  svc->Start(kTestPID);
  svc->OnDevicesChanged({"eth0", "eth1", "wlan0", "wlan1", "wwan0"}, {});
}

TEST_F(ArcServiceTest, VerifyAddrOrder) {
  ExpectGetDeviceProperties("wlan0", ShillClient::Device::Type::kWifi);
  ExpectGetDeviceProperties("eth0", ShillClient::Device::Type::kEthernet);
  EXPECT_CALL(*datapath_, NetnsAttachName(StrEq("arc_netns"), kTestPID))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arcbr0"), kArcHostIP, 30))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arc_eth0"), kFirstEthHostIP, 30))
      .Times(2)
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arc_wlan0"), kFirstWifiHostIP, 30))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_,
              ConnectVethPair(kTestPID, StrEq("arc_netns"), _, _, _, _, _, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(_, _)).WillRepeatedly(Return(true));

  auto svc = NewService(GuestMessage::ARC);
  svc->Start(kTestPID);
  svc->OnDevicesChanged({"wlan0"}, {});
  svc->OnDevicesChanged({"eth0"}, {});
  svc->OnDevicesChanged({}, {"eth0"});
  svc->OnDevicesChanged({"eth0"}, {});
}

TEST_F(ArcServiceTest, StableArcVmMacAddrs) {
  EXPECT_CALL(*datapath_, AddTAP(StrEq(""), _, nullptr, StrEq("crosvm")))
      .WillRepeatedly(Return("vmtap"));
  EXPECT_CALL(*datapath_, AddBridge(_, _, 30)).WillRepeatedly(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(_, _)).WillRepeatedly(Return(true));

  auto svc = NewService(GuestMessage::ARC_VM);
  svc->Start(kTestCID);
  auto configs = svc->GetDeviceConfigs();
  EXPECT_EQ(configs.size(), 6);
  auto mac_addr = kArcVmArc0MacAddr;
  for (const auto* config : configs) {
    EXPECT_EQ(config->mac_addr(), mac_addr);
    mac_addr[5]++;
  }
}

// ContainerImpl

TEST_F(ArcServiceTest, ContainerImpl_Start) {
  EXPECT_CALL(*datapath_, NetnsAttachName(StrEq("arc_netns"), kTestPID))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_,
              ConnectVethPair(kTestPID, StrEq("arc_netns"), StrEq("vetharc0"),
                              StrEq("arc0"), _, kArcGuestIP, 30, false))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(StrEq("arcbr0"), StrEq("vetharc0")))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arcbr0"), kArcHostIP, 30))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, SetConntrackHelpers(true)).WillOnce(Return(true));

  auto svc = NewService(GuestMessage::ARC);
  svc->Start(kTestPID);
  EXPECT_TRUE(svc->IsStarted());

  Mock::VerifyAndClearExpectations(datapath_.get());
}

TEST_F(ArcServiceTest, ContainerImpl_FailsToCreateInterface) {
  EXPECT_CALL(*datapath_, NetnsAttachName(StrEq("arc_netns"), kTestPID))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_,
              ConnectVethPair(kTestPID, StrEq("arc_netns"), StrEq("vetharc0"),
                              StrEq("arc0"), _, kArcGuestIP, 30, false))
      .WillOnce(Return(false));
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arcbr0"), kArcHostIP, 30)).Times(0);
  EXPECT_CALL(*datapath_, RemoveBridge(_)).Times(0);
  EXPECT_CALL(*datapath_, SetConntrackHelpers(_)).Times(0);

  auto svc = NewService(GuestMessage::ARC);
  svc->Start(kTestPID);
  EXPECT_FALSE(svc->IsStarted());
  Mock::VerifyAndClearExpectations(datapath_.get());
}

TEST_F(ArcServiceTest, ContainerImpl_FailsToAddInterfaceToBridge) {
  EXPECT_CALL(*datapath_, NetnsAttachName(StrEq("arc_netns"), kTestPID))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_,
              ConnectVethPair(kTestPID, StrEq("arc_netns"), StrEq("vetharc0"),
                              StrEq("arc0"), _, kArcGuestIP, 30, false))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arcbr0"), kArcHostIP, 30))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(StrEq("arcbr0"), StrEq("vetharc0")))
      .WillOnce(Return(false));
  EXPECT_CALL(*datapath_, RemoveInterface(_)).Times(0);
  EXPECT_CALL(*datapath_, RemoveBridge(_)).Times(0);
  EXPECT_CALL(*datapath_, SetConntrackHelpers(true)).Times(0);

  auto svc = NewService(GuestMessage::ARC);
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
                              StrEq("arc0"), _, kArcGuestIP, 30, false))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arcbr0"), kArcHostIP, 30))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(StrEq("arcbr0"), StrEq("vetharc0")))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, SetConntrackHelpers(true)).WillOnce(Return(true));

  auto svc = NewService(GuestMessage::ARC);
  svc->Start(kTestPID);
  EXPECT_TRUE(svc->IsStarted());
  Mock::VerifyAndClearExpectations(datapath_.get());

  // Expectations for eth0 setup.
  ExpectGetDeviceProperties("eth0", ShillClient::Device::Type::kEthernet);
  EXPECT_CALL(*datapath_,
              ConnectVethPair(kTestPID, StrEq("arc_netns"), StrEq("vetheth0"),
                              StrEq("eth0"), _, kFirstEthGuestIP, 30, false))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arc_eth0"), kFirstEthHostIP, 30))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(StrEq("arc_eth0"), StrEq("vetheth0")))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, StartRoutingDevice(StrEq("eth0"), StrEq("arc_eth0"),
                                             kFirstEthGuestIP,
                                             TrafficSource::ARC, false, _));

  svc->OnDevicesChanged({"eth0"}, {});
  Mock::VerifyAndClearExpectations(datapath_.get());
}

TEST_F(ArcServiceTest, ContainerImpl_ScanDevices) {
  EXPECT_CALL(*datapath_, NetnsAttachName(StrEq("arc_netns"), kTestPID))
      .WillOnce(Return(true));
  // Expectations for arc0 setup.
  EXPECT_CALL(*datapath_,
              ConnectVethPair(kTestPID, StrEq("arc_netns"), StrEq("vetharc0"),
                              StrEq("arc0"), _, kArcGuestIP, 30, false))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arcbr0"), kArcHostIP, 30))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(StrEq("arcbr0"), StrEq("vetharc0")))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, SetConntrackHelpers(true)).WillOnce(Return(true));

  auto svc = NewService(GuestMessage::ARC);
  svc->Start(kTestPID);
  EXPECT_TRUE(svc->IsStarted());
  Mock::VerifyAndClearExpectations(datapath_.get());

  ExpectGetDeviceProperties("eth0", ShillClient::Device::Type::kEthernet);
  ExpectGetDeviceProperties("wlan0", ShillClient::Device::Type::kWifi);
  EXPECT_CALL(*datapath_, ConnectVethPair(_, _, _, _, _, _, _, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*datapath_, AddBridge(_, _, _)).WillRepeatedly(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(_, _)).WillRepeatedly(Return(true));

  svc->OnDevicesChanged({"eth0", "wlan0"}, {});
  Mock::VerifyAndClearExpectations(datapath_.get());

  std::vector<std::string> devs;
  svc->ScanDevices(base::BindRepeating(
      [](std::vector<std::string>* list, const Device& device) {
        list->push_back(device.host_ifname());
      },
      &devs));

  EXPECT_EQ(devs.size(), 2);
  EXPECT_THAT(devs,
              UnorderedElementsAre(StrEq("arc_eth0"), StrEq("arc_wlan0")));
}

TEST_F(ArcServiceTest, ContainerImpl_DeviceHandler) {
  EXPECT_CALL(*datapath_, NetnsAttachName(StrEq("arc_netns"), kTestPID))
      .WillOnce(Return(true));
  // Expectations for arc0 setup.
  EXPECT_CALL(*datapath_,
              ConnectVethPair(kTestPID, StrEq("arc_netns"), StrEq("vetharc0"),
                              StrEq("arc0"), _, kArcGuestIP, 30, false))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arcbr0"), kArcHostIP, 30))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(StrEq("arcbr0"), StrEq("vetharc0")))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, SetConntrackHelpers(true)).WillOnce(Return(true));

  auto svc = NewService(GuestMessage::ARC);
  svc->Start(kTestPID);
  EXPECT_TRUE(svc->IsStarted());
  Mock::VerifyAndClearExpectations(datapath_.get());

  ExpectGetDeviceProperties("eth0", ShillClient::Device::Type::kEthernet);
  ExpectGetDeviceProperties("wlan0", ShillClient::Device::Type::kWifi);
  EXPECT_CALL(*datapath_, AddBridge(_, _, _)).WillRepeatedly(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(_, _)).WillRepeatedly(Return(true));
  EXPECT_CALL(*datapath_, ConnectVethPair(_, _, _, _, _, _, _, _))
      .WillRepeatedly(Return(true));

  svc->OnDevicesChanged({"eth0", "wlan0"}, {});
  EXPECT_EQ(guest_devices_.size(), 2);
  EXPECT_THAT(guest_devices_,
              UnorderedElementsAre(
                  Pair(StrEq("arc_eth0"), Device::ChangeEvent::ADDED),
                  Pair(StrEq("arc_wlan0"), Device::ChangeEvent::ADDED)));
  guest_devices_.clear();

  svc->OnDevicesChanged({}, {"wlan0"});
  EXPECT_THAT(guest_devices_,
              UnorderedElementsAre(
                  Pair(StrEq("arc_wlan0"), Device::ChangeEvent::REMOVED)));
  guest_devices_.clear();

  svc->OnDevicesChanged({"wlan0"}, {});
  Mock::VerifyAndClearExpectations(datapath_.get());
  EXPECT_THAT(guest_devices_,
              UnorderedElementsAre(
                  Pair(StrEq("arc_wlan0"), Device::ChangeEvent::ADDED)));
}

TEST_F(ArcServiceTest, ContainerImpl_StartAfterDevice) {
  ExpectGetDeviceProperties("eth0", ShillClient::Device::Type::kEthernet);
  EXPECT_CALL(*datapath_, NetnsAttachName(StrEq("arc_netns"), kTestPID))
      .WillOnce(Return(true));
  // Expectations for arc0 setup.
  EXPECT_CALL(*datapath_,
              ConnectVethPair(kTestPID, StrEq("arc_netns"), StrEq("vetharc0"),
                              StrEq("arc0"), _, kArcGuestIP, 30, false))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arcbr0"), kArcHostIP, 30))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(StrEq("arcbr0"), StrEq("vetharc0")))
      .WillOnce(Return(true));
  // Expectations for eth0 setup.
  EXPECT_CALL(*datapath_,
              ConnectVethPair(kTestPID, StrEq("arc_netns"), StrEq("vetheth0"),
                              StrEq("eth0"), _, kFirstEthGuestIP, 30, false))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arc_eth0"), kFirstEthHostIP, 30))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(StrEq("arc_eth0"), StrEq("vetheth0")))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, StartRoutingDevice(StrEq("eth0"), StrEq("arc_eth0"),
                                             kFirstEthGuestIP,
                                             TrafficSource::ARC, false, _));

  auto svc = NewService(GuestMessage::ARC);
  svc->OnDevicesChanged({"eth0"}, {});
  svc->Start(kTestPID);
  EXPECT_TRUE(svc->IsStarted());
  Mock::VerifyAndClearExpectations(datapath_.get());
}

TEST_F(ArcServiceTest, ContainerImpl_Stop) {
  EXPECT_CALL(*datapath_, NetnsAttachName(StrEq("arc_netns"), kTestPID))
      .WillOnce(Return(true));
  // Expectations for arc0 setup.
  EXPECT_CALL(*datapath_,
              ConnectVethPair(kTestPID, StrEq("arc_netns"), StrEq("vetharc0"),
                              StrEq("arc0"), _, kArcGuestIP, 30, false))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arcbr0"), kArcHostIP, 30))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(StrEq("arcbr0"), StrEq("vetharc0")))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, SetConntrackHelpers(true)).WillOnce(Return(true));

  auto svc = NewService(GuestMessage::ARC);
  svc->Start(kTestPID);
  EXPECT_TRUE(svc->IsStarted());
  Mock::VerifyAndClearExpectations(datapath_.get());

  // Expectations for eth0 setup.
  ExpectGetDeviceProperties("eth0", ShillClient::Device::Type::kEthernet);
  EXPECT_CALL(*datapath_,
              ConnectVethPair(kTestPID, StrEq("arc_netns"), StrEq("vetheth0"),
                              StrEq("eth0"), _, kFirstEthGuestIP, 30, false))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arc_eth0"), kFirstEthHostIP, 30))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(StrEq("arc_eth0"), StrEq("vetheth0")))
      .WillOnce(Return(true));

  svc->OnDevicesChanged({"eth0"}, {});
  Mock::VerifyAndClearExpectations(datapath_.get());

  // Expectations for arc0 teardown.
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
}

TEST_F(ArcServiceTest, ContainerImpl_OnStopDevice) {
  EXPECT_CALL(*datapath_, NetnsAttachName(StrEq("arc_netns"), kTestPID))
      .WillOnce(Return(true));
  // Expectations for arc0 setup.
  EXPECT_CALL(*datapath_,
              ConnectVethPair(kTestPID, StrEq("arc_netns"), StrEq("vetharc0"),
                              StrEq("arc0"), _, kArcGuestIP, 30, false))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arcbr0"), kArcHostIP, 30))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(StrEq("arcbr0"), StrEq("vetharc0")))
      .WillOnce(Return(true));

  auto svc = NewService(GuestMessage::ARC);
  svc->Start(kTestPID);
  EXPECT_TRUE(svc->IsStarted());
  Mock::VerifyAndClearExpectations(datapath_.get());

  ExpectGetDeviceProperties("eth0", ShillClient::Device::Type::kEthernet);
  // Expectations for eth0 setup.
  EXPECT_CALL(*datapath_,
              ConnectVethPair(kTestPID, StrEq("arc_netns"), StrEq("vetheth0"),
                              StrEq("eth0"), _, kFirstEthGuestIP, 30, false))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arc_eth0"), kFirstEthHostIP, 30))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(StrEq("arc_eth0"), StrEq("vetheth0")))
      .WillOnce(Return(true));

  svc->OnDevicesChanged({"eth0"}, {});
  Mock::VerifyAndClearExpectations(datapath_.get());

  // Expectations for eth0 teardown.
  EXPECT_CALL(*datapath_, RemoveInterface(StrEq("vetheth0"))).Times(1);
  EXPECT_CALL(*datapath_, StopRoutingDevice(StrEq("eth0"), StrEq("arc_eth0"),
                                            Ipv4Addr(100, 115, 92, 6),
                                            TrafficSource::ARC, false));
  EXPECT_CALL(*datapath_, RemoveBridge(StrEq("arc_eth0"))).Times(1);

  svc->OnDevicesChanged({}, {"eth0"});
  Mock::VerifyAndClearExpectations(datapath_.get());
}

// VM Impl

TEST_F(ArcServiceTest, VmImpl_Start) {
  // Expectations for tap devices pre-creation.
  EXPECT_CALL(*datapath_, AddTAP(StrEq(""), _, nullptr, StrEq("crosvm")))
      .WillOnce(Return("vmtap0"))
      .WillOnce(Return("vmtap1"))
      .WillOnce(Return("vmtap2"))
      .WillOnce(Return("vmtap3"))
      .WillOnce(Return("vmtap4"))
      .WillOnce(Return("vmtap5"));
  // Expectations for "arc0" setup.
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arcbr0"), kArcHostIP, 30))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(StrEq("arcbr0"), StrEq("vmtap0")))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, SetConntrackHelpers(true)).WillOnce(Return(true));

  auto svc = NewService(GuestMessage::ARC_VM);
  svc->Start(kTestPID);
  EXPECT_TRUE(svc->IsStarted());
  Mock::VerifyAndClearExpectations(datapath_.get());
}

TEST_F(ArcServiceTest, VmImpl_StartDevice) {
  ExpectGetDeviceProperties("eth0", ShillClient::Device::Type::kEthernet);
  // Expectations for tap devices pre-creation.
  EXPECT_CALL(*datapath_, AddTAP(StrEq(""), _, nullptr, StrEq("crosvm")))
      .WillOnce(Return("vmtap0"))
      .WillOnce(Return("vmtap1"))
      .WillOnce(Return("vmtap2"))
      .WillOnce(Return("vmtap3"))
      .WillOnce(Return("vmtap4"))
      .WillOnce(Return("vmtap5"));
  // Expectations for "arc0" setup.
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arcbr0"), kArcHostIP, 30))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(StrEq("arcbr0"), StrEq("vmtap0")))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, SetConntrackHelpers(true)).WillOnce(Return(true));

  auto svc = NewService(GuestMessage::ARC_VM);
  svc->Start(kTestPID);
  EXPECT_TRUE(svc->IsStarted());
  Mock::VerifyAndClearExpectations(datapath_.get());

  // Expectations for eth0 setup.
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arc_eth0"), kFirstEthHostIP, 30))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(StrEq("arc_eth0"), StrEq("vmtap1")))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, StartRoutingDevice(StrEq("eth0"), StrEq("arc_eth0"),
                                             Ipv4Addr(100, 115, 92, 6),
                                             TrafficSource::ARC, false, _));

  svc->OnDevicesChanged({"eth0"}, {});
  Mock::VerifyAndClearExpectations(datapath_.get());
}

TEST_F(ArcServiceTest, VmImpl_StartMultipleDevices) {
  // Expectations for tap devices pre-creation.
  EXPECT_CALL(*datapath_, AddTAP(StrEq(""), _, nullptr, StrEq("crosvm")))
      .WillOnce(Return("vmtap0"))
      .WillOnce(Return("vmtap1"))
      .WillOnce(Return("vmtap2"))
      .WillOnce(Return("vmtap3"))
      .WillOnce(Return("vmtap4"))
      .WillOnce(Return("vmtap5"));
  // Expectations for "arc0" setup.
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arcbr0"), kArcHostIP, 30))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(StrEq("arcbr0"), StrEq("vmtap0")))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, SetConntrackHelpers(true)).WillOnce(Return(true));

  auto svc = NewService(GuestMessage::ARC_VM);
  svc->Start(kTestPID);
  EXPECT_TRUE(svc->IsStarted());
  Mock::VerifyAndClearExpectations(datapath_.get());

  // Expectations for eth0 setup.
  ExpectGetDeviceProperties("eth0", ShillClient::Device::Type::kEthernet);
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arc_eth0"), kFirstEthHostIP, 30))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(StrEq("arc_eth0"), StrEq("vmtap1")))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, StartRoutingDevice(StrEq("eth0"), StrEq("arc_eth0"),
                                             Ipv4Addr(100, 115, 92, 6),
                                             TrafficSource::ARC, false, _));

  svc->OnDevicesChanged({"eth0"}, {});
  Mock::VerifyAndClearExpectations(datapath_.get());

  // Expectations for wlan0 setup.
  ExpectGetDeviceProperties("wlan0", ShillClient::Device::Type::kWifi);
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arc_wlan0"), kFirstWifiHostIP, 30))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(StrEq("arc_wlan0"), StrEq("vmtap3")))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, StartRoutingDevice(StrEq("wlan0"), StrEq("arc_wlan0"),
                                             Ipv4Addr(100, 115, 92, 14),
                                             TrafficSource::ARC, false, _));

  svc->OnDevicesChanged({"wlan0"}, {});
  Mock::VerifyAndClearExpectations(datapath_.get());

  // Expectations for eth1 setup.
  ExpectGetDeviceProperties("eth1", ShillClient::Device::Type::kEthernet);
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arc_eth1"), kSecondEthHostIP, 30))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(StrEq("arc_eth1"), StrEq("vmtap2")))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, StartRoutingDevice(StrEq("eth1"), StrEq("arc_eth1"),
                                             Ipv4Addr(100, 115, 92, 10),
                                             TrafficSource::ARC, false, _));

  svc->OnDevicesChanged({"eth1"}, {});
  Mock::VerifyAndClearExpectations(datapath_.get());
}

TEST_F(ArcServiceTest, VmImpl_Stop) {
  // Expectations for tap devices pre-creation.
  EXPECT_CALL(*datapath_, AddTAP(StrEq(""), _, nullptr, StrEq("crosvm")))
      .WillOnce(Return("vmtap0"))
      .WillOnce(Return("vmtap1"))
      .WillOnce(Return("vmtap2"))
      .WillOnce(Return("vmtap3"))
      .WillOnce(Return("vmtap4"))
      .WillOnce(Return("vmtap5"));
  // Expectations for "arc0" setup.
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arcbr0"), kArcHostIP, 30))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(StrEq("arcbr0"), StrEq("vmtap0")))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, SetConntrackHelpers(true)).WillOnce(Return(true));

  auto svc = NewService(GuestMessage::ARC_VM);
  svc->Start(kTestPID);
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

  svc->Stop(kTestPID);
  EXPECT_FALSE(svc->IsStarted());
  Mock::VerifyAndClearExpectations(datapath_.get());
}

TEST_F(ArcServiceTest, VmImpl_StopDevice) {
  // Expectations for tap devices pre-creation.
  EXPECT_CALL(*datapath_, AddTAP(StrEq(""), _, nullptr, StrEq("crosvm")))
      .WillOnce(Return("vmtap0"))
      .WillOnce(Return("vmtap1"))
      .WillOnce(Return("vmtap2"))
      .WillOnce(Return("vmtap3"))
      .WillOnce(Return("vmtap4"))
      .WillOnce(Return("vmtap5"));
  // Expectations for "arc0" setup.
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arcbr0"), kArcHostIP, 30))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(StrEq("arcbr0"), StrEq("vmtap0")))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, SetConntrackHelpers(true)).WillOnce(Return(true));

  auto svc = NewService(GuestMessage::ARC_VM);
  svc->Start(kTestPID);
  EXPECT_TRUE(svc->IsStarted());
  Mock::VerifyAndClearExpectations(datapath_.get());

  // Expectations for eth0 setup.
  ExpectGetDeviceProperties("eth0", ShillClient::Device::Type::kEthernet);
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arc_eth0"), kFirstEthHostIP, 30))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(StrEq("arc_eth0"), StrEq("vmtap1")))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, StartRoutingDevice(StrEq("eth0"), StrEq("arc_eth0"),
                                             Ipv4Addr(100, 115, 92, 6),
                                             TrafficSource::ARC, false, _));

  svc->OnDevicesChanged({"eth0"}, {});
  Mock::VerifyAndClearExpectations(datapath_.get());

  // Expectations for eth0 teardown.
  EXPECT_CALL(*datapath_, StopRoutingDevice(StrEq("eth0"), StrEq("arc_eth0"),
                                            Ipv4Addr(100, 115, 92, 6),
                                            TrafficSource::ARC, false));
  EXPECT_CALL(*datapath_, RemoveBridge(StrEq("arc_eth0")));

  svc->OnDevicesChanged({}, {"eth0"});
  Mock::VerifyAndClearExpectations(datapath_.get());
}

TEST_F(ArcServiceTest, VmImpl_ScanDevices) {
  // Expectations for tap devices pre-creation.
  EXPECT_CALL(*datapath_, AddTAP(StrEq(""), _, nullptr, StrEq("crosvm")))
      .WillOnce(Return("vmtap0"))
      .WillOnce(Return("vmtap1"))
      .WillOnce(Return("vmtap2"))
      .WillOnce(Return("vmtap3"))
      .WillOnce(Return("vmtap4"))
      .WillOnce(Return("vmtap5"));
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arcbr0"), kArcHostIP, 30))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(StrEq("arcbr0"), StrEq("vmtap0")))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, SetConntrackHelpers(true)).WillOnce(Return(true));

  auto svc = NewService(GuestMessage::ARC_VM);
  svc->Start(kTestPID);
  EXPECT_TRUE(svc->IsStarted());
  Mock::VerifyAndClearExpectations(datapath_.get());

  ExpectGetDeviceProperties("eth0", ShillClient::Device::Type::kEthernet);
  ExpectGetDeviceProperties("eth1", ShillClient::Device::Type::kEthernet);
  ExpectGetDeviceProperties("wlan0", ShillClient::Device::Type::kWifi);
  EXPECT_CALL(*datapath_, AddBridge(_, _, _)).WillRepeatedly(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(_, _)).WillRepeatedly(Return(true));

  svc->OnDevicesChanged({"eth0", "wlan0", "eth1"}, {});
  Mock::VerifyAndClearExpectations(datapath_.get());

  std::vector<std::string> devs;
  svc->ScanDevices(base::BindRepeating(
      [](std::vector<std::string>* list, const Device& device) {
        list->push_back(device.host_ifname());
      },
      &devs));

  EXPECT_EQ(devs.size(), 3);
  EXPECT_THAT(devs, UnorderedElementsAre(StrEq("arc_eth0"), StrEq("arc_wlan0"),
                                         StrEq("arc_eth1")));
}

TEST_F(ArcServiceTest, VmImpl_DeviceHandler) {
  // Expectations for tap devices pre-creation.
  EXPECT_CALL(*datapath_, AddTAP(StrEq(""), _, nullptr, StrEq("crosvm")))
      .WillOnce(Return("vmtap0"))
      .WillOnce(Return("vmtap1"))
      .WillOnce(Return("vmtap2"))
      .WillOnce(Return("vmtap3"))
      .WillOnce(Return("vmtap4"))
      .WillOnce(Return("vmtap5"));
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arcbr0"), kArcHostIP, 30))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(StrEq("arcbr0"), StrEq("vmtap0")))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, SetConntrackHelpers(true)).WillOnce(Return(true));

  auto svc = NewService(GuestMessage::ARC_VM);
  svc->Start(kTestPID);
  EXPECT_TRUE(svc->IsStarted());
  Mock::VerifyAndClearExpectations(datapath_.get());

  ExpectGetDeviceProperties("eth0", ShillClient::Device::Type::kEthernet);
  ExpectGetDeviceProperties("wlan0", ShillClient::Device::Type::kWifi);
  EXPECT_CALL(*datapath_, AddBridge(_, _, _)).WillRepeatedly(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(_, _)).WillRepeatedly(Return(true));

  svc->OnDevicesChanged({"eth0", "wlan0"}, {});
  EXPECT_THAT(guest_devices_,
              UnorderedElementsAre(
                  Pair(StrEq("arc_eth0"), Device::ChangeEvent::ADDED),
                  Pair(StrEq("arc_wlan0"), Device::ChangeEvent::ADDED)));
  guest_devices_.clear();

  svc->OnDevicesChanged({}, {"wlan0"});
  EXPECT_THAT(guest_devices_,
              UnorderedElementsAre(
                  Pair(StrEq("arc_wlan0"), Device::ChangeEvent::REMOVED)));
  guest_devices_.clear();

  svc->OnDevicesChanged({"wlan0"}, {});
  Mock::VerifyAndClearExpectations(datapath_.get());
  EXPECT_THAT(guest_devices_,
              UnorderedElementsAre(
                  Pair(StrEq("arc_wlan0"), Device::ChangeEvent::ADDED)));
}

}  // namespace patchpanel
