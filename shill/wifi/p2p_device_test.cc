// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/wifi/p2p_device.h"

#include <memory>
#include <string>
#include <utility>

#include <base/test/mock_callback.h>
#include <net-base/byte_utils.h>
#include <net-base/mac_address.h>

#include "shill/mock_control.h"
#include "shill/mock_event_dispatcher.h"
#include "shill/mock_manager.h"
#include "shill/mock_metrics.h"
#include "shill/supplicant/mock_supplicant_group_proxy.h"
#include "shill/supplicant/mock_supplicant_interface_proxy.h"
#include "shill/supplicant/mock_supplicant_p2pdevice_proxy.h"
#include "shill/test_event_dispatcher.h"
#include "shill/wifi/local_device.h"
#include "shill/wifi/mock_p2p_manager.h"
#include "shill/wifi/mock_p2p_service.h"
#include "shill/wifi/mock_wifi_phy.h"
#include "shill/wifi/mock_wifi_provider.h"
#include "shill/wifi/wifi_security.h"

using ::testing::_;
using ::testing::ByMove;
using ::testing::DoAll;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::StrictMock;
using ::testing::Test;

namespace shill {

namespace {
const char kPrimaryInterfaceName[] = "wlan0";
const char kInterfaceName[] = "p2p-wlan0-0";
const RpcIdentifier kInterfacePath = RpcIdentifier("/interface/p2p-wlan0-0");
const RpcIdentifier kGroupPath =
    RpcIdentifier("/interface/p2p-wlan0-0/Groups/xx");
const uint32_t kPhyIndex = 5678;
const uint32_t kShillId = 0;
const char kP2PSSID[] = "chromeOS-1234";
const char kP2PBSSID[] = "de:ad:be:ef:00:00";
const char kP2PPassphrase[] = "test0000";
const uint32_t kP2PFrequency = 2437;

}  // namespace

class P2PDeviceTest : public testing::Test {
 public:
  P2PDeviceTest()
      : manager_(&control_interface_, &dispatcher_, &metrics_),
        wifi_provider_(new NiceMock<MockWiFiProvider>(&manager_)),
        wifi_phy_(kPhyIndex),
        p2p_manager_(new NiceMock<MockP2PManager>(&manager_)),
        supplicant_primary_p2pdevice_proxy_(
            new NiceMock<MockSupplicantP2PDeviceProxy>()),
        supplicant_p2pdevice_proxy_(
            new NiceMock<MockSupplicantP2PDeviceProxy>()),
        supplicant_group_proxy_(new NiceMock<MockSupplicantGroupProxy>()),
        supplicant_interface_proxy_(
            new NiceMock<MockSupplicantInterfaceProxy>()) {
    // Replace the WiFi provider's P2PManager with a mock.
    wifi_provider_->p2p_manager_.reset(p2p_manager_);
    // Replace the Manager's WiFi provider with a mock.
    manager_.wifi_provider_.reset(wifi_provider_);
    // Update the Manager's map from technology to provider.
    manager_.UpdateProviderMapping();
    ON_CALL(*wifi_provider_, GetPhyAtIndex(kPhyIndex))
        .WillByDefault(Return(&wifi_phy_));
    ON_CALL(*supplicant_primary_p2pdevice_proxy_, GroupAdd(_))
        .WillByDefault(Return(true));
    ON_CALL(*supplicant_primary_p2pdevice_proxy_, AddPersistentGroup(_, _))
        .WillByDefault(DoAll(SetArgPointee<1>(kGroupPath), Return(true)));
    ON_CALL(*supplicant_p2pdevice_proxy_, Disconnect())
        .WillByDefault(Return(true));
    ON_CALL(*supplicant_group_proxy_, GetSSID(_))
        .WillByDefault(DoAll(
            SetArgPointee<0>(net_base::byte_utils::ByteStringToBytes(kP2PSSID)),
            Return(true)));
    ON_CALL(*supplicant_group_proxy_, GetBSSID(_))
        .WillByDefault(DoAll(
            SetArgPointee<0>(
                net_base::MacAddress::CreateFromString(kP2PBSSID)->ToBytes()),
            Return(true)));
    ON_CALL(*supplicant_group_proxy_, GetFrequency(_))
        .WillByDefault(DoAll(SetArgPointee<0>(kP2PFrequency), Return(true)));
    ON_CALL(*supplicant_group_proxy_, GetPassphrase(_))
        .WillByDefault(DoAll(SetArgPointee<0>(kP2PPassphrase), Return(true)));
    ON_CALL(*supplicant_interface_proxy_, GetIfname(_))
        .WillByDefault(DoAll(SetArgPointee<0>(kInterfaceName), Return(true)));
    ON_CALL(*p2p_manager_, SupplicantPrimaryP2PDeviceProxy())
        .WillByDefault(Return(supplicant_primary_p2pdevice_proxy_.get()));
    std::unique_ptr<SupplicantP2PDeviceProxyInterface> p2pdevice_proxy(
        supplicant_p2pdevice_proxy_);
    ON_CALL(control_interface_, CreateSupplicantP2PDeviceProxy(_, _))
        .WillByDefault(Return(ByMove(std::move(p2pdevice_proxy))));
    std::unique_ptr<SupplicantGroupProxyInterface> group_proxy(
        supplicant_group_proxy_);
    ON_CALL(control_interface_, CreateSupplicantGroupProxy(_, _))
        .WillByDefault(Return(ByMove(std::move(group_proxy))));
    std::unique_ptr<SupplicantInterfaceProxyInterface> interface_proxy(
        supplicant_interface_proxy_);
    ON_CALL(control_interface_, CreateSupplicantInterfaceProxy(_, _))
        .WillByDefault(Return(ByMove(std::move(interface_proxy))));
  }

  KeyValueStore DefaultGroupStartedProperties() {
    KeyValueStore properties;
    properties.Set<RpcIdentifier>(
        WPASupplicant::kGroupStartedPropertyInterfaceObject, kInterfacePath);
    properties.Set<RpcIdentifier>(
        WPASupplicant::kGroupStartedPropertyGroupObject, kGroupPath);
    return properties;
  }

  KeyValueStore DefaultGroupFinishedProperties() {
    KeyValueStore properties;
    properties.Set<RpcIdentifier>(
        WPASupplicant::kGroupFinishedPropertyInterfaceObject, kInterfacePath);
    properties.Set<RpcIdentifier>(
        WPASupplicant::kGroupFinishedPropertyGroupObject, kGroupPath);
    return properties;
  }

 protected:
  StrictMock<base::MockRepeatingCallback<void(LocalDevice::DeviceEvent,
                                              const LocalDevice*)>>
      cb;

  NiceMock<MockControl> control_interface_;
  EventDispatcherForTest dispatcher_;
  NiceMock<MockMetrics> metrics_;
  NiceMock<MockManager> manager_;
  MockWiFiProvider* wifi_provider_;
  MockWiFiPhy wifi_phy_;
  MockP2PManager* p2p_manager_;
  std::unique_ptr<MockSupplicantP2PDeviceProxy>
      supplicant_primary_p2pdevice_proxy_;
  MockSupplicantP2PDeviceProxy* supplicant_p2pdevice_proxy_;
  MockSupplicantGroupProxy* supplicant_group_proxy_;
  MockSupplicantInterfaceProxy* supplicant_interface_proxy_;
};

TEST_F(P2PDeviceTest, DeviceOnOff) {
  scoped_refptr<P2PDevice> device =
      new P2PDevice(&manager_, LocalDevice::IfaceType::kP2PGO,
                    kPrimaryInterfaceName, kPhyIndex, kShillId, cb.Get());
  CHECK_EQ(device->state_, P2PDevice::P2PDeviceState::kUninitialized);
  device->Start();
  CHECK_EQ(device->state_, P2PDevice::P2PDeviceState::kReady);
  device->Stop();
  CHECK_EQ(device->state_, P2PDevice::P2PDeviceState::kUninitialized);
}

TEST_F(P2PDeviceTest, GroupInfo) {
  scoped_refptr<P2PDevice> device =
      new P2PDevice(&manager_, LocalDevice::IfaceType::kP2PGO,
                    kPrimaryInterfaceName, kPhyIndex, kShillId, cb.Get());

  KeyValueStore groupInfo = device->GetGroupInfo();
  EXPECT_TRUE(groupInfo.Contains<Integer>(kP2PGroupInfoShillIDProperty));
  EXPECT_TRUE(groupInfo.Contains<String>(kP2PGroupInfoStateProperty));

  EXPECT_EQ(groupInfo.Get<Integer>(kP2PGroupInfoShillIDProperty), kShillId);
  EXPECT_EQ(groupInfo.Get<String>(kP2PGroupInfoStateProperty),
            kP2PGroupInfoStateIdle);
}

TEST_F(P2PDeviceTest, ClientInfo) {
  scoped_refptr<P2PDevice> device =
      new P2PDevice(&manager_, LocalDevice::IfaceType::kP2PClient,
                    kPrimaryInterfaceName, kPhyIndex, kShillId, cb.Get());

  KeyValueStore clientInfo = device->GetClientInfo();
  EXPECT_TRUE(clientInfo.Contains<Integer>(kP2PGroupInfoShillIDProperty));
  EXPECT_TRUE(clientInfo.Contains<String>(kP2PGroupInfoStateProperty));

  EXPECT_EQ(clientInfo.Get<Integer>(kP2PGroupInfoShillIDProperty), kShillId);
  EXPECT_EQ(clientInfo.Get<String>(kP2PGroupInfoStateProperty),
            kP2PGroupInfoStateIdle);
}

TEST_F(P2PDeviceTest, CreateAndRemove) {
  scoped_refptr<P2PDevice> device =
      new P2PDevice(&manager_, LocalDevice::IfaceType::kP2PGO,
                    kPrimaryInterfaceName, kPhyIndex, kShillId, cb.Get());
  // Start device
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kUninitialized);
  EXPECT_TRUE(device->Start());
  EXPECT_EQ(device->service_, nullptr);
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kReady);

  // Initiate group creation with a new service.
  auto service0 = std::make_unique<MockP2PService>(
      device, kP2PSSID, kP2PPassphrase, kP2PFrequency);
  EXPECT_CALL(*supplicant_primary_p2pdevice_proxy_, GroupAdd(_));
  EXPECT_TRUE(device->CreateGroup(std::move(service0)));
  EXPECT_NE(device->service_, nullptr);
  EXPECT_EQ(device->service_->state(),
            LocalService::LocalServiceState::kStateStarting);
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kGOStarting);

  // Emulate GroupStarted signal from wpa_supplicant.
  EXPECT_CALL(control_interface_,
              CreateSupplicantInterfaceProxy(_, kInterfacePath));
  EXPECT_CALL(control_interface_,
              CreateSupplicantP2PDeviceProxy(_, kInterfacePath));
  device->GroupStarted(DefaultGroupStartedProperties());
  EXPECT_NE(device->supplicant_interface_proxy_, nullptr);
  EXPECT_NE(device->supplicant_p2pdevice_proxy_, nullptr);
  EXPECT_NE(device->supplicant_group_proxy_, nullptr);
  EXPECT_EQ(device->link_name_.value(), kInterfaceName);
  EXPECT_EQ(device->group_ssid_, kP2PSSID);
  EXPECT_EQ(device->group_bssid_, kP2PBSSID);
  EXPECT_EQ(device->group_frequency_, kP2PFrequency);
  EXPECT_EQ(device->group_passphrase_, kP2PPassphrase);
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kGOConfiguring);

  // Attempting to create group again should be a no-op and and return false.
  auto service1 = std::make_unique<MockP2PService>(
      device, kP2PSSID, kP2PPassphrase, kP2PFrequency);
  EXPECT_CALL(*supplicant_primary_p2pdevice_proxy_, GroupAdd(_)).Times(0);
  EXPECT_FALSE(device->CreateGroup(std::move(service1)));
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kGOConfiguring);

  // Remove group.
  EXPECT_CALL(*supplicant_p2pdevice_proxy_, Disconnect());
  EXPECT_TRUE(device->RemoveGroup());
  EXPECT_EQ(device->service_, nullptr);
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kGOStopping);

  // Emulate GroupFinished signal from wpa_supplicant
  device->GroupFinished(DefaultGroupFinishedProperties());
  EXPECT_EQ(device->supplicant_interface_proxy_, nullptr);
  EXPECT_EQ(device->supplicant_p2pdevice_proxy_, nullptr);
  EXPECT_EQ(device->supplicant_group_proxy_, nullptr);
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kReady);

  // Stop device
  EXPECT_TRUE(device->Stop());
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kUninitialized);
}

TEST_F(P2PDeviceTest, ConnectAndDisconnect) {
  scoped_refptr<P2PDevice> device =
      new P2PDevice(&manager_, LocalDevice::IfaceType::kP2PGO,
                    kPrimaryInterfaceName, kPhyIndex, kShillId, cb.Get());
  // Start device
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kUninitialized);
  EXPECT_TRUE(device->Start());
  EXPECT_EQ(device->service_, nullptr);
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kReady);

  // Initiate connection with a new service.
  auto service0 = std::make_unique<MockP2PService>(
      device, kP2PSSID, kP2PPassphrase, kP2PFrequency);
  EXPECT_CALL(*supplicant_primary_p2pdevice_proxy_, AddPersistentGroup(_, _));
  EXPECT_CALL(*supplicant_primary_p2pdevice_proxy_, GroupAdd(_));
  EXPECT_TRUE(device->Connect(std::move(service0)));
  EXPECT_NE(device->service_, nullptr);
  EXPECT_EQ(device->service_->state(),
            LocalService::LocalServiceState::kStateStarting);
  EXPECT_EQ(device->supplicant_persistent_group_path_, kGroupPath);
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kClientAssociating);

  // Emulate GroupStarted signal from wpa_supplicant.
  EXPECT_CALL(control_interface_,
              CreateSupplicantInterfaceProxy(_, kInterfacePath));
  EXPECT_CALL(control_interface_,
              CreateSupplicantP2PDeviceProxy(_, kInterfacePath));
  device->GroupStarted(DefaultGroupStartedProperties());
  EXPECT_NE(device->supplicant_interface_proxy_, nullptr);
  EXPECT_NE(device->supplicant_p2pdevice_proxy_, nullptr);
  EXPECT_NE(device->supplicant_group_proxy_, nullptr);
  EXPECT_EQ(device->link_name_.value(), kInterfaceName);
  EXPECT_EQ(device->group_ssid_, kP2PSSID);
  EXPECT_EQ(device->group_bssid_, kP2PBSSID);
  EXPECT_EQ(device->group_frequency_, kP2PFrequency);
  EXPECT_EQ(device->group_passphrase_, kP2PPassphrase);
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kClientConfiguring);

  // Attempting to connect again should be a no-op and and return false.
  auto service1 = std::make_unique<MockP2PService>(
      device, kP2PSSID, kP2PPassphrase, kP2PFrequency);
  EXPECT_CALL(*supplicant_primary_p2pdevice_proxy_, AddPersistentGroup(_, _))
      .Times(0);
  EXPECT_CALL(*supplicant_primary_p2pdevice_proxy_, GroupAdd(_)).Times(0);
  EXPECT_FALSE(device->Connect(std::move(service1)));
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kClientConfiguring);

  // Disconnect.
  EXPECT_CALL(*supplicant_p2pdevice_proxy_, Disconnect());
  EXPECT_TRUE(device->Disconnect());
  EXPECT_EQ(device->service_, nullptr);
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kClientDisconnecting);

  // Emulate GroupFinished signal from wpa_supplicant
  EXPECT_CALL(*supplicant_primary_p2pdevice_proxy_, RemovePersistentGroup(_));
  device->GroupFinished(DefaultGroupFinishedProperties());
  EXPECT_EQ(device->supplicant_interface_proxy_, nullptr);
  EXPECT_EQ(device->supplicant_p2pdevice_proxy_, nullptr);
  EXPECT_EQ(device->supplicant_group_proxy_, nullptr);
  EXPECT_TRUE(device->supplicant_persistent_group_path_.value().empty());
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kReady);

  // Stop device
  EXPECT_TRUE(device->Stop());
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kUninitialized);
}

TEST_F(P2PDeviceTest, BadState_Client) {
  scoped_refptr<P2PDevice> device =
      new P2PDevice(&manager_, LocalDevice::IfaceType::kP2PClient,
                    kPrimaryInterfaceName, kPhyIndex, kShillId, cb.Get());

  // Initiate connection while device is uninitialized
  auto service = std::make_unique<MockP2PService>(
      device, kP2PSSID, kP2PPassphrase, kP2PFrequency);
  EXPECT_CALL(*supplicant_primary_p2pdevice_proxy_, AddPersistentGroup(_, _))
      .Times(0);
  EXPECT_CALL(*supplicant_primary_p2pdevice_proxy_, GroupAdd(_)).Times(0);
  EXPECT_FALSE(device->Connect(std::move(service)));
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kUninitialized);

  // Disconnect while not connected
  EXPECT_FALSE(device->Disconnect());
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kUninitialized);

  // Start device
  EXPECT_TRUE(device->Start());
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kReady);

  // Initiate connection with a new service.
  service = std::make_unique<MockP2PService>(device, kP2PSSID, kP2PPassphrase,
                                             kP2PFrequency);
  EXPECT_CALL(*supplicant_primary_p2pdevice_proxy_, AddPersistentGroup(_, _));
  EXPECT_CALL(*supplicant_primary_p2pdevice_proxy_, GroupAdd(_));
  EXPECT_TRUE(device->Connect(std::move(service)));
  EXPECT_NE(device->service_, nullptr);
  EXPECT_EQ(device->service_->state(),
            LocalService::LocalServiceState::kStateStarting);
  EXPECT_EQ(device->supplicant_persistent_group_path_, kGroupPath);
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kClientAssociating);

  // Emulate GroupStarted signal from wpa_supplicant.
  EXPECT_CALL(control_interface_,
              CreateSupplicantInterfaceProxy(_, kInterfacePath));
  EXPECT_CALL(control_interface_,
              CreateSupplicantP2PDeviceProxy(_, kInterfacePath));
  device->GroupStarted(DefaultGroupStartedProperties());
  EXPECT_NE(device->supplicant_interface_proxy_, nullptr);
  EXPECT_NE(device->supplicant_p2pdevice_proxy_, nullptr);
  EXPECT_NE(device->supplicant_group_proxy_, nullptr);
  EXPECT_EQ(device->link_name_.value(), kInterfaceName);
  EXPECT_EQ(device->group_ssid_, kP2PSSID);
  EXPECT_EQ(device->group_bssid_, kP2PBSSID);
  EXPECT_EQ(device->group_frequency_, kP2PFrequency);
  EXPECT_EQ(device->group_passphrase_, kP2PPassphrase);
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kClientConfiguring);

  // Attempting to connect again should be a no-op and and return false.
  service = std::make_unique<MockP2PService>(device, kP2PSSID, kP2PPassphrase,
                                             kP2PFrequency);
  EXPECT_CALL(*supplicant_primary_p2pdevice_proxy_, AddPersistentGroup(_, _))
      .Times(0);
  EXPECT_CALL(*supplicant_primary_p2pdevice_proxy_, GroupAdd(_)).Times(0);
  EXPECT_FALSE(device->Connect(std::move(service)));
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kClientConfiguring);

  // Disconnect.
  EXPECT_CALL(*supplicant_p2pdevice_proxy_, Disconnect());
  EXPECT_TRUE(device->Disconnect());
  EXPECT_EQ(device->service_, nullptr);
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kClientDisconnecting);

  // Emulate GroupFinished signal from wpa_supplicant
  EXPECT_CALL(*supplicant_primary_p2pdevice_proxy_, RemovePersistentGroup(_));
  device->GroupFinished(DefaultGroupFinishedProperties());
  EXPECT_EQ(device->supplicant_interface_proxy_, nullptr);
  EXPECT_EQ(device->supplicant_p2pdevice_proxy_, nullptr);
  EXPECT_EQ(device->supplicant_group_proxy_, nullptr);
  EXPECT_TRUE(device->supplicant_persistent_group_path_.value().empty());
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kReady);

  // Stop device
  EXPECT_TRUE(device->Stop());
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kUninitialized);

  // Initiate connection while device is uninitialized
  service = std::make_unique<MockP2PService>(device, kP2PSSID, kP2PPassphrase,
                                             kP2PFrequency);
  EXPECT_CALL(*supplicant_primary_p2pdevice_proxy_, AddPersistentGroup(_, _))
      .Times(0);
  EXPECT_CALL(*supplicant_primary_p2pdevice_proxy_, GroupAdd(_)).Times(0);
  EXPECT_FALSE(device->Connect(std::move(service)));
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kUninitialized);

  // Disconnect while not connected
  EXPECT_FALSE(device->Disconnect());
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kUninitialized);
}

TEST_F(P2PDeviceTest, BadState_GO) {
  scoped_refptr<P2PDevice> device =
      new P2PDevice(&manager_, LocalDevice::IfaceType::kP2PGO,
                    kPrimaryInterfaceName, kPhyIndex, kShillId, cb.Get());

  // Initiate group creation while device is uninitialized
  auto service = std::make_unique<MockP2PService>(
      device, kP2PSSID, kP2PPassphrase, kP2PFrequency);
  EXPECT_CALL(*supplicant_primary_p2pdevice_proxy_, GroupAdd(_)).Times(0);
  EXPECT_FALSE(device->CreateGroup(std::move(service)));
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kUninitialized);

  // Remove group while not created
  EXPECT_FALSE(device->RemoveGroup());
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kUninitialized);

  // Start device
  EXPECT_TRUE(device->Start());
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kReady);

  // Initiate group creation with a new service.
  service = std::make_unique<MockP2PService>(device, kP2PSSID, kP2PPassphrase,
                                             kP2PFrequency);
  EXPECT_CALL(*supplicant_primary_p2pdevice_proxy_, GroupAdd(_));
  EXPECT_TRUE(device->CreateGroup(std::move(service)));
  EXPECT_NE(device->service_, nullptr);
  EXPECT_EQ(device->service_->state(),
            LocalService::LocalServiceState::kStateStarting);
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kGOStarting);

  // Emulate GroupStarted signal from wpa_supplicant.
  EXPECT_CALL(control_interface_,
              CreateSupplicantInterfaceProxy(_, kInterfacePath));
  EXPECT_CALL(control_interface_,
              CreateSupplicantP2PDeviceProxy(_, kInterfacePath));
  device->GroupStarted(DefaultGroupStartedProperties());
  EXPECT_NE(device->supplicant_interface_proxy_, nullptr);
  EXPECT_NE(device->supplicant_p2pdevice_proxy_, nullptr);
  EXPECT_NE(device->supplicant_group_proxy_, nullptr);
  EXPECT_EQ(device->link_name_.value(), kInterfaceName);
  EXPECT_EQ(device->group_ssid_, kP2PSSID);
  EXPECT_EQ(device->group_bssid_, kP2PBSSID);
  EXPECT_EQ(device->group_frequency_, kP2PFrequency);
  EXPECT_EQ(device->group_passphrase_, kP2PPassphrase);
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kGOConfiguring);

  // Attempting to create group again should be a no-op and and return false.
  service = std::make_unique<MockP2PService>(device, kP2PSSID, kP2PPassphrase,
                                             kP2PFrequency);
  EXPECT_CALL(*supplicant_primary_p2pdevice_proxy_, GroupAdd(_)).Times(0);
  EXPECT_FALSE(device->CreateGroup(std::move(service)));
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kGOConfiguring);

  // Remove group.
  EXPECT_CALL(*supplicant_p2pdevice_proxy_, Disconnect());
  EXPECT_TRUE(device->RemoveGroup());
  EXPECT_EQ(device->service_, nullptr);
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kGOStopping);

  // Emulate GroupFinished signal from wpa_supplicant
  device->GroupFinished(DefaultGroupFinishedProperties());
  EXPECT_EQ(device->supplicant_interface_proxy_, nullptr);
  EXPECT_EQ(device->supplicant_p2pdevice_proxy_, nullptr);
  EXPECT_EQ(device->supplicant_group_proxy_, nullptr);
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kReady);

  // Stop device
  EXPECT_TRUE(device->Stop());
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kUninitialized);

  // Initiate group creation while device is uninitialized
  service = std::make_unique<MockP2PService>(device, kP2PSSID, kP2PPassphrase,
                                             kP2PFrequency);
  EXPECT_CALL(*supplicant_primary_p2pdevice_proxy_, GroupAdd(_)).Times(0);
  EXPECT_FALSE(device->CreateGroup(std::move(service)));
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kUninitialized);

  // Remove group while not created
  EXPECT_FALSE(device->RemoveGroup());
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kUninitialized);
}

TEST_F(P2PDeviceTest, ConnectToSupplicantInterfaceProxy) {
  scoped_refptr<P2PDevice> device =
      new P2PDevice(&manager_, LocalDevice::IfaceType::kP2PGO,
                    kPrimaryInterfaceName, kPhyIndex, kShillId, cb.Get());
  EXPECT_CALL(control_interface_,
              CreateSupplicantInterfaceProxy(_, kInterfacePath));
  EXPECT_TRUE(device->ConnectToSupplicantInterfaceProxy(kInterfacePath));
  EXPECT_NE(device->supplicant_interface_proxy_, nullptr);
}

TEST_F(P2PDeviceTest, ConnectToSupplicantInterfaceProxy_WhileConnected) {
  scoped_refptr<P2PDevice> device =
      new P2PDevice(&manager_, LocalDevice::IfaceType::kP2PGO,
                    kPrimaryInterfaceName, kPhyIndex, kShillId, cb.Get());
  EXPECT_CALL(control_interface_,
              CreateSupplicantInterfaceProxy(_, kInterfacePath));
  EXPECT_TRUE(device->ConnectToSupplicantInterfaceProxy(kInterfacePath));
  EXPECT_NE(device->supplicant_interface_proxy_, nullptr);

  EXPECT_CALL(control_interface_, CreateSupplicantInterfaceProxy(_, _))
      .Times(0);
  EXPECT_FALSE(device->ConnectToSupplicantInterfaceProxy(kInterfacePath));
  EXPECT_NE(device->supplicant_interface_proxy_, nullptr);
}

TEST_F(P2PDeviceTest, ConnectToSupplicantInterfaceProxy_Failure) {
  scoped_refptr<P2PDevice> device =
      new P2PDevice(&manager_, LocalDevice::IfaceType::kP2PGO,
                    kPrimaryInterfaceName, kPhyIndex, kShillId, cb.Get());
  EXPECT_CALL(control_interface_,
              CreateSupplicantInterfaceProxy(_, kInterfacePath))
      .WillOnce(Return(nullptr));
  EXPECT_FALSE(device->ConnectToSupplicantInterfaceProxy(kInterfacePath));
  EXPECT_EQ(device->supplicant_interface_proxy_, nullptr);
}

TEST_F(P2PDeviceTest, ConnectToSupplicantP2PDeviceProxy) {
  scoped_refptr<P2PDevice> device =
      new P2PDevice(&manager_, LocalDevice::IfaceType::kP2PGO,
                    kPrimaryInterfaceName, kPhyIndex, kShillId, cb.Get());
  EXPECT_CALL(control_interface_,
              CreateSupplicantP2PDeviceProxy(_, kInterfacePath));
  EXPECT_TRUE(device->ConnectToSupplicantP2PDeviceProxy(kInterfacePath));
  EXPECT_NE(device->supplicant_p2pdevice_proxy_, nullptr);
}

TEST_F(P2PDeviceTest, ConnectToSupplicantP2PDeviceProxy_WhileConnected) {
  scoped_refptr<P2PDevice> device =
      new P2PDevice(&manager_, LocalDevice::IfaceType::kP2PGO,
                    kPrimaryInterfaceName, kPhyIndex, kShillId, cb.Get());
  EXPECT_CALL(control_interface_,
              CreateSupplicantP2PDeviceProxy(_, kInterfacePath));
  EXPECT_TRUE(device->ConnectToSupplicantP2PDeviceProxy(kInterfacePath));
  EXPECT_NE(device->supplicant_p2pdevice_proxy_, nullptr);

  EXPECT_CALL(control_interface_, CreateSupplicantP2PDeviceProxy(_, _))
      .Times(0);
  EXPECT_FALSE(device->ConnectToSupplicantP2PDeviceProxy(kInterfacePath));
  EXPECT_NE(device->supplicant_p2pdevice_proxy_, nullptr);
}

TEST_F(P2PDeviceTest, ConnectToSupplicantP2PDeviceProxy_Failure) {
  scoped_refptr<P2PDevice> device =
      new P2PDevice(&manager_, LocalDevice::IfaceType::kP2PGO,
                    kPrimaryInterfaceName, kPhyIndex, kShillId, cb.Get());
  EXPECT_CALL(control_interface_,
              CreateSupplicantP2PDeviceProxy(_, kInterfacePath))
      .WillOnce(Return(nullptr));
  EXPECT_FALSE(device->ConnectToSupplicantP2PDeviceProxy(kInterfacePath));
  EXPECT_EQ(device->supplicant_p2pdevice_proxy_, nullptr);
}

TEST_F(P2PDeviceTest, ConnectToSupplicantGroupProxy) {
  scoped_refptr<P2PDevice> device =
      new P2PDevice(&manager_, LocalDevice::IfaceType::kP2PGO,
                    kPrimaryInterfaceName, kPhyIndex, kShillId, cb.Get());
  EXPECT_CALL(control_interface_, CreateSupplicantGroupProxy(_, kGroupPath));
  EXPECT_TRUE(device->ConnectToSupplicantGroupProxy(kGroupPath));
  EXPECT_NE(device->supplicant_group_proxy_, nullptr);
}

TEST_F(P2PDeviceTest, ConnectToSupplicantGroupProxy_WhileConnected) {
  scoped_refptr<P2PDevice> device =
      new P2PDevice(&manager_, LocalDevice::IfaceType::kP2PGO,
                    kPrimaryInterfaceName, kPhyIndex, kShillId, cb.Get());
  EXPECT_CALL(control_interface_, CreateSupplicantGroupProxy(_, kGroupPath));
  EXPECT_TRUE(device->ConnectToSupplicantGroupProxy(kGroupPath));
  EXPECT_NE(device->supplicant_group_proxy_, nullptr);

  EXPECT_CALL(control_interface_, CreateSupplicantGroupProxy(_, _)).Times(0);
  EXPECT_FALSE(device->ConnectToSupplicantGroupProxy(kGroupPath));
  EXPECT_NE(device->supplicant_group_proxy_, nullptr);
}

TEST_F(P2PDeviceTest, ConnectToSupplicantGroupProxy_Failure) {
  scoped_refptr<P2PDevice> device =
      new P2PDevice(&manager_, LocalDevice::IfaceType::kP2PGO,
                    kPrimaryInterfaceName, kPhyIndex, kShillId, cb.Get());
  EXPECT_CALL(control_interface_, CreateSupplicantGroupProxy(_, kGroupPath))
      .WillOnce(Return(nullptr));
  EXPECT_FALSE(device->ConnectToSupplicantGroupProxy(kGroupPath));
  EXPECT_EQ(device->supplicant_group_proxy_, nullptr);
}

TEST_F(P2PDeviceTest, SetupGroup) {
  KeyValueStore properties = DefaultGroupStartedProperties();
  scoped_refptr<P2PDevice> device =
      new P2PDevice(&manager_, LocalDevice::IfaceType::kP2PGO,
                    kPrimaryInterfaceName, kPhyIndex, kShillId, cb.Get());
  EXPECT_CALL(control_interface_,
              CreateSupplicantInterfaceProxy(_, kInterfacePath));
  EXPECT_CALL(control_interface_,
              CreateSupplicantP2PDeviceProxy(_, kInterfacePath));
  EXPECT_CALL(control_interface_, CreateSupplicantGroupProxy(_, kGroupPath));
  device->SetupGroup(properties);
  EXPECT_NE(device->supplicant_interface_proxy_, nullptr);
  EXPECT_NE(device->supplicant_p2pdevice_proxy_, nullptr);
  EXPECT_NE(device->supplicant_group_proxy_, nullptr);
  EXPECT_EQ(device->link_name_.value(), kInterfaceName);
  EXPECT_EQ(device->group_ssid_, kP2PSSID);
  EXPECT_EQ(device->group_bssid_, kP2PBSSID);
  EXPECT_EQ(device->group_frequency_, kP2PFrequency);
  EXPECT_EQ(device->group_passphrase_, kP2PPassphrase);
}

TEST_F(P2PDeviceTest, SetupGroup_EmptyProperties) {
  KeyValueStore properties;
  scoped_refptr<P2PDevice> device =
      new P2PDevice(&manager_, LocalDevice::IfaceType::kP2PGO,
                    kPrimaryInterfaceName, kPhyIndex, kShillId, cb.Get());
  EXPECT_CALL(control_interface_, CreateSupplicantInterfaceProxy(_, _))
      .Times(0);
  EXPECT_CALL(control_interface_, CreateSupplicantP2PDeviceProxy(_, _))
      .Times(0);
  EXPECT_CALL(control_interface_, CreateSupplicantGroupProxy(_, _)).Times(0);
  device->SetupGroup(properties);
  EXPECT_EQ(device->supplicant_interface_proxy_, nullptr);
  EXPECT_EQ(device->supplicant_p2pdevice_proxy_, nullptr);
  EXPECT_EQ(device->supplicant_group_proxy_, nullptr);
}

TEST_F(P2PDeviceTest, SetupGroup_MissingGroupPath) {
  KeyValueStore properties;
  properties.Set<RpcIdentifier>(
      WPASupplicant::kGroupStartedPropertyInterfaceObject, kInterfacePath);
  scoped_refptr<P2PDevice> device =
      new P2PDevice(&manager_, LocalDevice::IfaceType::kP2PGO,
                    kPrimaryInterfaceName, kPhyIndex, kShillId, cb.Get());
  EXPECT_CALL(control_interface_, CreateSupplicantInterfaceProxy(_, _))
      .Times(0);
  EXPECT_CALL(control_interface_, CreateSupplicantP2PDeviceProxy(_, _))
      .Times(0);
  EXPECT_CALL(control_interface_, CreateSupplicantGroupProxy(_, _)).Times(0);
  device->SetupGroup(properties);
  EXPECT_EQ(device->supplicant_interface_proxy_, nullptr);
  EXPECT_EQ(device->supplicant_p2pdevice_proxy_, nullptr);
  EXPECT_EQ(device->supplicant_group_proxy_, nullptr);
}

TEST_F(P2PDeviceTest, GroupStarted_WhileNotExpected) {
  scoped_refptr<P2PDevice> device =
      new P2PDevice(&manager_, LocalDevice::IfaceType::kP2PGO,
                    kPrimaryInterfaceName, kPhyIndex, kShillId, cb.Get());
  // Start device
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kUninitialized);
  EXPECT_TRUE(device->Start());
  CHECK_EQ(device->state_, P2PDevice::P2PDeviceState::kReady);

  // Ignore unextected signal from wpa_supplicant.
  device->GroupStarted(DefaultGroupStartedProperties());
  CHECK_EQ(device->state_, P2PDevice::P2PDeviceState::kReady);

  // Stop device
  device->Stop();
  CHECK_EQ(device->state_, P2PDevice::P2PDeviceState::kUninitialized);
}

TEST_F(P2PDeviceTest, GroupFinished_WhileGOStarting) {
  scoped_refptr<P2PDevice> device =
      new P2PDevice(&manager_, LocalDevice::IfaceType::kP2PGO,
                    kPrimaryInterfaceName, kPhyIndex, kShillId, cb.Get());
  // Start device
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kUninitialized);
  EXPECT_TRUE(device->Start());
  EXPECT_EQ(device->service_, nullptr);
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kReady);

  // Initiate group creation with a new service.
  auto service = std::make_unique<MockP2PService>(
      device, kP2PSSID, kP2PPassphrase, kP2PFrequency);
  EXPECT_CALL(*supplicant_primary_p2pdevice_proxy_, GroupAdd(_));
  EXPECT_TRUE(device->CreateGroup(std::move(service)));
  EXPECT_NE(device->service_, nullptr);
  EXPECT_EQ(device->service_->state(),
            LocalService::LocalServiceState::kStateStarting);
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kGOStarting);

  // Emulate GroupFinished signal from wpa_supplicant (unknown failure).
  // Uexpected signal, we are going to ignore finished signal for group
  // which was never started. Let the start timer to expire.
  device->GroupFinished(DefaultGroupFinishedProperties());
  EXPECT_EQ(device->supplicant_interface_proxy_, nullptr);
  EXPECT_EQ(device->supplicant_p2pdevice_proxy_, nullptr);
  EXPECT_EQ(device->supplicant_group_proxy_, nullptr);
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kGOStarting);

  // TODO(b:308081318): Implement start/stop timers
  // Emulate start timer expired.
  device->SetState(P2PDevice::P2PDeviceState::kGOStopping);

  // Stop device
  device->Stop();
  CHECK_EQ(device->state_, P2PDevice::P2PDeviceState::kUninitialized);
}

TEST_F(P2PDeviceTest, GroupFinished_WhileGOConfiguring) {
  scoped_refptr<P2PDevice> device =
      new P2PDevice(&manager_, LocalDevice::IfaceType::kP2PGO,
                    kPrimaryInterfaceName, kPhyIndex, kShillId, cb.Get());
  // Start device
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kUninitialized);
  EXPECT_TRUE(device->Start());
  EXPECT_EQ(device->service_, nullptr);
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kReady);

  // Initiate group creation with a new service.
  auto service = std::make_unique<MockP2PService>(
      device, kP2PSSID, kP2PPassphrase, kP2PFrequency);
  EXPECT_CALL(*supplicant_primary_p2pdevice_proxy_, GroupAdd(_));
  EXPECT_TRUE(device->CreateGroup(std::move(service)));
  EXPECT_NE(device->service_, nullptr);
  EXPECT_EQ(device->service_->state(),
            LocalService::LocalServiceState::kStateStarting);
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kGOStarting);

  // Emulate GroupStarted signal from wpa_supplicant.
  EXPECT_CALL(control_interface_,
              CreateSupplicantInterfaceProxy(_, kInterfacePath));
  EXPECT_CALL(control_interface_,
              CreateSupplicantP2PDeviceProxy(_, kInterfacePath));
  device->GroupStarted(DefaultGroupStartedProperties());
  EXPECT_NE(device->supplicant_interface_proxy_, nullptr);
  EXPECT_NE(device->supplicant_p2pdevice_proxy_, nullptr);
  EXPECT_NE(device->supplicant_group_proxy_, nullptr);
  EXPECT_EQ(device->link_name_.value(), kInterfaceName);
  EXPECT_EQ(device->group_ssid_, kP2PSSID);
  EXPECT_EQ(device->group_bssid_, kP2PBSSID);
  EXPECT_EQ(device->group_frequency_, kP2PFrequency);
  EXPECT_EQ(device->group_passphrase_, kP2PPassphrase);
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kGOConfiguring);

  // Emulate GroupFinished signal from wpa_supplicant (link failure).
  device->GroupFinished(DefaultGroupFinishedProperties());
  EXPECT_EQ(device->supplicant_interface_proxy_, nullptr);
  EXPECT_EQ(device->supplicant_p2pdevice_proxy_, nullptr);
  EXPECT_EQ(device->supplicant_group_proxy_, nullptr);
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kGOStopping);

  // Stop device
  device->Stop();
  CHECK_EQ(device->state_, P2PDevice::P2PDeviceState::kUninitialized);
}

TEST_F(P2PDeviceTest, GroupFinished_WhileGOActive) {
  scoped_refptr<P2PDevice> device =
      new P2PDevice(&manager_, LocalDevice::IfaceType::kP2PGO,
                    kPrimaryInterfaceName, kPhyIndex, kShillId, cb.Get());
  // Start device
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kUninitialized);
  EXPECT_TRUE(device->Start());
  EXPECT_EQ(device->service_, nullptr);
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kReady);

  // Initiate group creation with a new service.
  auto service = std::make_unique<MockP2PService>(
      device, kP2PSSID, kP2PPassphrase, kP2PFrequency);
  EXPECT_CALL(*supplicant_primary_p2pdevice_proxy_, GroupAdd(_));
  EXPECT_TRUE(device->CreateGroup(std::move(service)));
  EXPECT_NE(device->service_, nullptr);
  EXPECT_EQ(device->service_->state(),
            LocalService::LocalServiceState::kStateStarting);
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kGOStarting);

  // Emulate GroupStarted signal from wpa_supplicant.
  EXPECT_CALL(control_interface_,
              CreateSupplicantInterfaceProxy(_, kInterfacePath));
  EXPECT_CALL(control_interface_,
              CreateSupplicantP2PDeviceProxy(_, kInterfacePath));
  device->GroupStarted(DefaultGroupStartedProperties());
  EXPECT_NE(device->supplicant_interface_proxy_, nullptr);
  EXPECT_NE(device->supplicant_p2pdevice_proxy_, nullptr);
  EXPECT_NE(device->supplicant_group_proxy_, nullptr);
  EXPECT_EQ(device->link_name_.value(), kInterfaceName);
  EXPECT_EQ(device->group_ssid_, kP2PSSID);
  EXPECT_EQ(device->group_bssid_, kP2PBSSID);
  EXPECT_EQ(device->group_frequency_, kP2PFrequency);
  EXPECT_EQ(device->group_passphrase_, kP2PPassphrase);
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kGOConfiguring);

  // Emulate network layer initialization.
  device->SetState(P2PDevice::P2PDeviceState::kGOActive);

  // Emulate GroupFinished signal from wpa_supplicant (link failure).
  device->GroupFinished(DefaultGroupFinishedProperties());
  EXPECT_EQ(device->supplicant_interface_proxy_, nullptr);
  EXPECT_EQ(device->supplicant_p2pdevice_proxy_, nullptr);
  EXPECT_EQ(device->supplicant_group_proxy_, nullptr);
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kGOStopping);

  // Stop device
  device->Stop();
  CHECK_EQ(device->state_, P2PDevice::P2PDeviceState::kUninitialized);
}

TEST_F(P2PDeviceTest, GroupFinished_WhileClientAssociating) {
  scoped_refptr<P2PDevice> device =
      new P2PDevice(&manager_, LocalDevice::IfaceType::kP2PClient,
                    kPrimaryInterfaceName, kPhyIndex, kShillId, cb.Get());
  // Start device
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kUninitialized);
  EXPECT_TRUE(device->Start());
  EXPECT_EQ(device->service_, nullptr);
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kReady);

  // Initiate group creation with a new service.
  auto service = std::make_unique<MockP2PService>(
      device, kP2PSSID, kP2PPassphrase, kP2PFrequency);
  EXPECT_CALL(*supplicant_primary_p2pdevice_proxy_, GroupAdd(_));
  EXPECT_TRUE(device->Connect(std::move(service)));
  EXPECT_NE(device->service_, nullptr);
  EXPECT_EQ(device->service_->state(),
            LocalService::LocalServiceState::kStateStarting);
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kClientAssociating);

  // Emulate GroupFinished signal from wpa_supplicant (unknown failure).
  // Uexpected signal, we are going to ignore finished signal for group
  // which was never started. Let the start timer to expire.
  device->GroupFinished(DefaultGroupFinishedProperties());
  EXPECT_EQ(device->supplicant_interface_proxy_, nullptr);
  EXPECT_EQ(device->supplicant_p2pdevice_proxy_, nullptr);
  EXPECT_EQ(device->supplicant_group_proxy_, nullptr);
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kClientAssociating);

  // TODO(b:308081318): Implement start/stop timers
  // Emulate start timer expired.
  device->SetState(P2PDevice::P2PDeviceState::kClientDisconnecting);

  // Stop device
  device->Stop();
  CHECK_EQ(device->state_, P2PDevice::P2PDeviceState::kUninitialized);
}

TEST_F(P2PDeviceTest, GroupFinished_WhileClientConfiguring) {
  scoped_refptr<P2PDevice> device =
      new P2PDevice(&manager_, LocalDevice::IfaceType::kP2PClient,
                    kPrimaryInterfaceName, kPhyIndex, kShillId, cb.Get());
  // Start device
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kUninitialized);
  EXPECT_TRUE(device->Start());
  EXPECT_EQ(device->service_, nullptr);
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kReady);

  // Initiate group creation with a new service.
  auto service = std::make_unique<MockP2PService>(
      device, kP2PSSID, kP2PPassphrase, kP2PFrequency);
  EXPECT_CALL(*supplicant_primary_p2pdevice_proxy_, GroupAdd(_));
  EXPECT_TRUE(device->Connect(std::move(service)));
  EXPECT_NE(device->service_, nullptr);
  EXPECT_EQ(device->service_->state(),
            LocalService::LocalServiceState::kStateStarting);
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kClientAssociating);

  // Emulate GroupStarted signal from wpa_supplicant.
  EXPECT_CALL(control_interface_,
              CreateSupplicantInterfaceProxy(_, kInterfacePath));
  EXPECT_CALL(control_interface_,
              CreateSupplicantP2PDeviceProxy(_, kInterfacePath));
  device->GroupStarted(DefaultGroupStartedProperties());
  EXPECT_NE(device->supplicant_interface_proxy_, nullptr);
  EXPECT_NE(device->supplicant_p2pdevice_proxy_, nullptr);
  EXPECT_NE(device->supplicant_group_proxy_, nullptr);
  EXPECT_EQ(device->link_name_.value(), kInterfaceName);
  EXPECT_EQ(device->group_ssid_, kP2PSSID);
  EXPECT_EQ(device->group_bssid_, kP2PBSSID);
  EXPECT_EQ(device->group_frequency_, kP2PFrequency);
  EXPECT_EQ(device->group_passphrase_, kP2PPassphrase);
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kClientConfiguring);

  // Emulate GroupFinished signal from wpa_supplicant (link failure).
  device->GroupFinished(DefaultGroupFinishedProperties());
  EXPECT_EQ(device->supplicant_interface_proxy_, nullptr);
  EXPECT_EQ(device->supplicant_p2pdevice_proxy_, nullptr);
  EXPECT_EQ(device->supplicant_group_proxy_, nullptr);
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kClientDisconnecting);

  // Stop device
  device->Stop();
  CHECK_EQ(device->state_, P2PDevice::P2PDeviceState::kUninitialized);
}

TEST_F(P2PDeviceTest, GroupFinished_WhileClientConnected) {
  scoped_refptr<P2PDevice> device =
      new P2PDevice(&manager_, LocalDevice::IfaceType::kP2PClient,
                    kPrimaryInterfaceName, kPhyIndex, kShillId, cb.Get());
  // Start device
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kUninitialized);
  EXPECT_TRUE(device->Start());
  EXPECT_EQ(device->service_, nullptr);
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kReady);

  // Initiate group creation with a new service.
  auto service = std::make_unique<MockP2PService>(
      device, kP2PSSID, kP2PPassphrase, kP2PFrequency);
  EXPECT_CALL(*supplicant_primary_p2pdevice_proxy_, GroupAdd(_));
  EXPECT_TRUE(device->Connect(std::move(service)));
  EXPECT_NE(device->service_, nullptr);
  EXPECT_EQ(device->service_->state(),
            LocalService::LocalServiceState::kStateStarting);
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kClientAssociating);

  // Emulate GroupStarted signal from wpa_supplicant.
  EXPECT_CALL(control_interface_,
              CreateSupplicantInterfaceProxy(_, kInterfacePath));
  EXPECT_CALL(control_interface_,
              CreateSupplicantP2PDeviceProxy(_, kInterfacePath));
  device->GroupStarted(DefaultGroupStartedProperties());
  EXPECT_NE(device->supplicant_interface_proxy_, nullptr);
  EXPECT_NE(device->supplicant_p2pdevice_proxy_, nullptr);
  EXPECT_NE(device->supplicant_group_proxy_, nullptr);
  EXPECT_EQ(device->link_name_.value(), kInterfaceName);
  EXPECT_EQ(device->group_ssid_, kP2PSSID);
  EXPECT_EQ(device->group_bssid_, kP2PBSSID);
  EXPECT_EQ(device->group_frequency_, kP2PFrequency);
  EXPECT_EQ(device->group_passphrase_, kP2PPassphrase);
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kClientConfiguring);

  // Emulate network layer initialization.
  device->SetState(P2PDevice::P2PDeviceState::kClientConnected);

  // Emulate GroupFinished signal from wpa_supplicant (link failure).
  device->GroupFinished(DefaultGroupFinishedProperties());
  EXPECT_EQ(device->supplicant_interface_proxy_, nullptr);
  EXPECT_EQ(device->supplicant_p2pdevice_proxy_, nullptr);
  EXPECT_EQ(device->supplicant_group_proxy_, nullptr);
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kClientDisconnecting);

  // Stop device
  device->Stop();
  CHECK_EQ(device->state_, P2PDevice::P2PDeviceState::kUninitialized);
}

TEST_F(P2PDeviceTest, GroupFinished_WhileNotExpected) {
  scoped_refptr<P2PDevice> device =
      new P2PDevice(&manager_, LocalDevice::IfaceType::kP2PGO,
                    kPrimaryInterfaceName, kPhyIndex, kShillId, cb.Get());
  // Start device
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kUninitialized);
  EXPECT_TRUE(device->Start());
  CHECK_EQ(device->state_, P2PDevice::P2PDeviceState::kReady);

  // Ignore unextected signal from wpa_supplicant.
  device->GroupFinished(DefaultGroupFinishedProperties());
  CHECK_EQ(device->state_, P2PDevice::P2PDeviceState::kReady);

  // Stop device
  device->Stop();
  CHECK_EQ(device->state_, P2PDevice::P2PDeviceState::kUninitialized);
}

}  // namespace shill
