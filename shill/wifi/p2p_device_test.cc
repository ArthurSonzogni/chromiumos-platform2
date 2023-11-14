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
#include "shill/supplicant/mock_supplicant_peer_proxy.h"
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

  dbus::ObjectPath DefaultPeerObjectPath(int peer_id) {
    return dbus::ObjectPath(
        std::string("/interface/") + std::string(kInterfaceName) +
        std::string("/Peers/deadbeef01") + std::to_string(peer_id));
  }

  ByteArray DefaultPeerAddress(int peer_id) {
    std::string mac_address =
        std::string("de:ad:be:ef:01:0") + std::to_string(peer_id);
    return net_base::MacAddress::CreateFromString(mac_address)->ToBytes();
  }

  KeyValueStore DefaultPeerProperties(int peer_id) {
    KeyValueStore properties;
    properties.Set<ByteArray>(WPASupplicant::kPeerPropertyDeviceAddress,
                              DefaultPeerAddress(peer_id));
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
  // Start device
  EXPECT_TRUE(device->Start());
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kReady);

  KeyValueStore group_info = device->GetGroupInfo();
  EXPECT_TRUE(group_info.Contains<Integer>(kP2PGroupInfoShillIDProperty));
  EXPECT_TRUE(group_info.Contains<String>(kP2PGroupInfoStateProperty));
  EXPECT_FALSE(group_info.Contains<String>(kP2PGroupInfoSSIDProperty));
  EXPECT_FALSE(group_info.Contains<String>(kP2PGroupInfoBSSIDProperty));
  EXPECT_FALSE(group_info.Contains<Integer>(kP2PGroupInfoFrequencyProperty));
  EXPECT_FALSE(group_info.Contains<String>(kP2PGroupInfoPassphraseProperty));
  EXPECT_TRUE(group_info.Contains<Stringmaps>(kP2PGroupInfoClientsProperty));

  EXPECT_EQ(group_info.Get<Integer>(kP2PGroupInfoShillIDProperty), kShillId);
  EXPECT_EQ(group_info.Get<String>(kP2PGroupInfoStateProperty),
            kP2PGroupInfoStateIdle);
  EXPECT_EQ(group_info.Get<Stringmaps>(kP2PGroupInfoClientsProperty).size(), 0);

  // Initiate group creation.
  auto service = std::make_unique<MockP2PService>(
      device, kP2PSSID, kP2PPassphrase, kP2PFrequency);
  EXPECT_TRUE(device->CreateGroup(std::move(service)));
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kGOStarting);

  group_info = device->GetGroupInfo();
  EXPECT_TRUE(group_info.Contains<Integer>(kP2PGroupInfoShillIDProperty));
  EXPECT_TRUE(group_info.Contains<String>(kP2PGroupInfoStateProperty));
  EXPECT_FALSE(group_info.Contains<String>(kP2PGroupInfoSSIDProperty));
  EXPECT_FALSE(group_info.Contains<String>(kP2PGroupInfoBSSIDProperty));
  EXPECT_FALSE(group_info.Contains<Integer>(kP2PGroupInfoFrequencyProperty));
  EXPECT_FALSE(group_info.Contains<String>(kP2PGroupInfoPassphraseProperty));
  EXPECT_TRUE(group_info.Contains<Stringmaps>(kP2PGroupInfoClientsProperty));

  EXPECT_EQ(group_info.Get<Integer>(kP2PGroupInfoShillIDProperty), kShillId);
  EXPECT_EQ(group_info.Get<String>(kP2PGroupInfoStateProperty),
            kP2PGroupInfoStateStarting);
  EXPECT_EQ(group_info.Get<Stringmaps>(kP2PGroupInfoClientsProperty).size(), 0);

  // Emulate GroupStarted signal from wpa_supplicant.
  device->GroupStarted(DefaultGroupStartedProperties());
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kGOConfiguring);

  group_info = device->GetGroupInfo();
  EXPECT_TRUE(group_info.Contains<Integer>(kP2PGroupInfoShillIDProperty));
  EXPECT_TRUE(group_info.Contains<String>(kP2PGroupInfoStateProperty));
  EXPECT_TRUE(group_info.Contains<String>(kP2PGroupInfoSSIDProperty));
  EXPECT_TRUE(group_info.Contains<String>(kP2PGroupInfoBSSIDProperty));
  EXPECT_TRUE(group_info.Contains<Integer>(kP2PGroupInfoFrequencyProperty));
  EXPECT_TRUE(group_info.Contains<String>(kP2PGroupInfoPassphraseProperty));
  EXPECT_TRUE(group_info.Contains<Stringmaps>(kP2PGroupInfoClientsProperty));

  EXPECT_EQ(group_info.Get<Integer>(kP2PGroupInfoShillIDProperty), kShillId);
  EXPECT_EQ(group_info.Get<String>(kP2PGroupInfoStateProperty),
            kP2PGroupInfoStateConfiguring);
  EXPECT_EQ(group_info.Get<String>(kP2PGroupInfoSSIDProperty), kP2PSSID);
  EXPECT_EQ(group_info.Get<String>(kP2PGroupInfoBSSIDProperty), kP2PBSSID);
  EXPECT_EQ(group_info.Get<Integer>(kP2PGroupInfoFrequencyProperty),
            kP2PFrequency);
  EXPECT_EQ(group_info.Get<String>(kP2PGroupInfoPassphraseProperty),
            kP2PPassphrase);
  EXPECT_EQ(group_info.Get<Stringmaps>(kP2PGroupInfoClientsProperty).size(), 0);

  // Emulate network layer initialization.
  device->SetState(P2PDevice::P2PDeviceState::kGOActive);

  group_info = device->GetGroupInfo();
  EXPECT_TRUE(group_info.Contains<Integer>(kP2PGroupInfoShillIDProperty));
  EXPECT_TRUE(group_info.Contains<String>(kP2PGroupInfoStateProperty));
  EXPECT_TRUE(group_info.Contains<String>(kP2PGroupInfoSSIDProperty));
  EXPECT_TRUE(group_info.Contains<String>(kP2PGroupInfoBSSIDProperty));
  EXPECT_TRUE(group_info.Contains<Integer>(kP2PGroupInfoFrequencyProperty));
  EXPECT_TRUE(group_info.Contains<String>(kP2PGroupInfoPassphraseProperty));
  EXPECT_TRUE(group_info.Contains<Stringmaps>(kP2PGroupInfoClientsProperty));

  EXPECT_EQ(group_info.Get<Integer>(kP2PGroupInfoShillIDProperty), kShillId);
  EXPECT_EQ(group_info.Get<String>(kP2PGroupInfoStateProperty),
            kP2PGroupInfoStateActive);
  EXPECT_EQ(group_info.Get<String>(kP2PGroupInfoSSIDProperty), kP2PSSID);
  EXPECT_EQ(group_info.Get<String>(kP2PGroupInfoBSSIDProperty), kP2PBSSID);
  EXPECT_EQ(group_info.Get<Integer>(kP2PGroupInfoFrequencyProperty),
            kP2PFrequency);
  EXPECT_EQ(group_info.Get<String>(kP2PGroupInfoPassphraseProperty),
            kP2PPassphrase);
  EXPECT_EQ(group_info.Get<Stringmaps>(kP2PGroupInfoClientsProperty).size(), 0);

  // Emulate PeerJoined signals from wpa_supplicant.
  int num_of_peers = 10;
  EXPECT_CALL(control_interface_, CreateSupplicantPeerProxy(_))
      .Times(num_of_peers);
  for (int peer_id = 0; peer_id < num_of_peers; peer_id++) {
    auto peer_proxy = std::make_unique<MockSupplicantPeerProxy>();
    auto peer_properties = DefaultPeerProperties(peer_id);
    auto peer_path = DefaultPeerObjectPath(peer_id);

    EXPECT_CALL(*peer_proxy, GetProperties(_))
        .WillOnce(DoAll(SetArgPointee<0>(peer_properties), Return(true)));
    ON_CALL(control_interface_, CreateSupplicantPeerProxy(_))
        .WillByDefault(Return(ByMove(std::move(peer_proxy))));
    device->PeerJoined(peer_path);

    EXPECT_TRUE(base::Contains(device->group_peers_, peer_path));
    EXPECT_EQ(device->group_peers_[peer_path], peer_properties);
    EXPECT_EQ(device->group_peers_.size(), peer_id + 1);
  }

  group_info = device->GetGroupInfo();
  EXPECT_TRUE(group_info.Contains<Integer>(kP2PGroupInfoShillIDProperty));
  EXPECT_TRUE(group_info.Contains<String>(kP2PGroupInfoStateProperty));
  EXPECT_TRUE(group_info.Contains<String>(kP2PGroupInfoSSIDProperty));
  EXPECT_TRUE(group_info.Contains<String>(kP2PGroupInfoBSSIDProperty));
  EXPECT_TRUE(group_info.Contains<Integer>(kP2PGroupInfoFrequencyProperty));
  EXPECT_TRUE(group_info.Contains<String>(kP2PGroupInfoPassphraseProperty));
  EXPECT_TRUE(group_info.Contains<Stringmaps>(kP2PGroupInfoClientsProperty));

  EXPECT_EQ(group_info.Get<Integer>(kP2PGroupInfoShillIDProperty), kShillId);
  EXPECT_EQ(group_info.Get<String>(kP2PGroupInfoStateProperty),
            kP2PGroupInfoStateActive);
  EXPECT_EQ(group_info.Get<String>(kP2PGroupInfoSSIDProperty), kP2PSSID);
  EXPECT_EQ(group_info.Get<String>(kP2PGroupInfoBSSIDProperty), kP2PBSSID);
  EXPECT_EQ(group_info.Get<Integer>(kP2PGroupInfoFrequencyProperty),
            kP2PFrequency);
  EXPECT_EQ(group_info.Get<String>(kP2PGroupInfoPassphraseProperty),
            kP2PPassphrase);
  EXPECT_EQ(group_info.Get<Stringmaps>(kP2PGroupInfoClientsProperty).size(),
            num_of_peers);

  auto group_clients = group_info.Get<Stringmaps>(kP2PGroupInfoClientsProperty);
  for (auto const& client : group_clients)
    EXPECT_TRUE(base::Contains(client, kP2PGroupInfoClientMACAddressProperty));

  // Remove group.
  EXPECT_TRUE(device->RemoveGroup());
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kGOStopping);

  group_info = device->GetGroupInfo();
  EXPECT_TRUE(group_info.Contains<Integer>(kP2PGroupInfoShillIDProperty));
  EXPECT_TRUE(group_info.Contains<String>(kP2PGroupInfoStateProperty));
  EXPECT_TRUE(group_info.Contains<String>(kP2PGroupInfoSSIDProperty));
  EXPECT_TRUE(group_info.Contains<String>(kP2PGroupInfoBSSIDProperty));
  EXPECT_TRUE(group_info.Contains<Integer>(kP2PGroupInfoFrequencyProperty));
  EXPECT_TRUE(group_info.Contains<String>(kP2PGroupInfoPassphraseProperty));
  EXPECT_TRUE(group_info.Contains<Stringmaps>(kP2PGroupInfoClientsProperty));

  EXPECT_EQ(group_info.Get<Integer>(kP2PGroupInfoShillIDProperty), kShillId);
  EXPECT_EQ(group_info.Get<String>(kP2PGroupInfoStateProperty),
            kP2PGroupInfoStateStopping);
  EXPECT_EQ(group_info.Get<String>(kP2PGroupInfoSSIDProperty), kP2PSSID);
  EXPECT_EQ(group_info.Get<String>(kP2PGroupInfoBSSIDProperty), kP2PBSSID);
  EXPECT_EQ(group_info.Get<Integer>(kP2PGroupInfoFrequencyProperty),
            kP2PFrequency);
  EXPECT_EQ(group_info.Get<String>(kP2PGroupInfoPassphraseProperty),
            kP2PPassphrase);
  EXPECT_EQ(group_info.Get<Stringmaps>(kP2PGroupInfoClientsProperty).size(),
            num_of_peers);

  // Emulate GroupFinished signal from wpa_supplicant
  device->GroupFinished(DefaultGroupFinishedProperties());
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kReady);

  group_info = device->GetGroupInfo();
  EXPECT_TRUE(group_info.Contains<Integer>(kP2PGroupInfoShillIDProperty));
  EXPECT_TRUE(group_info.Contains<String>(kP2PGroupInfoStateProperty));
  EXPECT_FALSE(group_info.Contains<String>(kP2PGroupInfoSSIDProperty));
  EXPECT_FALSE(group_info.Contains<String>(kP2PGroupInfoBSSIDProperty));
  EXPECT_FALSE(group_info.Contains<Integer>(kP2PGroupInfoFrequencyProperty));
  EXPECT_FALSE(group_info.Contains<String>(kP2PGroupInfoPassphraseProperty));
  EXPECT_TRUE(group_info.Contains<Stringmaps>(kP2PGroupInfoClientsProperty));

  EXPECT_EQ(group_info.Get<Integer>(kP2PGroupInfoShillIDProperty), kShillId);
  EXPECT_EQ(group_info.Get<String>(kP2PGroupInfoStateProperty),
            kP2PGroupInfoStateIdle);
  EXPECT_EQ(group_info.Get<Stringmaps>(kP2PGroupInfoClientsProperty).size(), 0);

  // Stop device
  EXPECT_TRUE(device->Stop());
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kUninitialized);
}

TEST_F(P2PDeviceTest, GroupInfo_EmptyOnClient) {
  scoped_refptr<P2PDevice> device =
      new P2PDevice(&manager_, LocalDevice::IfaceType::kP2PClient,
                    kPrimaryInterfaceName, kPhyIndex, kShillId, cb.Get());
  KeyValueStore group_info = device->GetGroupInfo();
  EXPECT_TRUE(group_info.IsEmpty());
}

TEST_F(P2PDeviceTest, ClientInfo) {
  scoped_refptr<P2PDevice> device =
      new P2PDevice(&manager_, LocalDevice::IfaceType::kP2PClient,
                    kPrimaryInterfaceName, kPhyIndex, kShillId, cb.Get());
  // Start device
  EXPECT_TRUE(device->Start());
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kReady);

  KeyValueStore client_info = device->GetClientInfo();
  EXPECT_TRUE(client_info.Contains<Integer>(kP2PClientInfoShillIDProperty));
  EXPECT_TRUE(client_info.Contains<String>(kP2PClientInfoStateProperty));
  EXPECT_FALSE(client_info.Contains<String>(kP2PClientInfoSSIDProperty));
  EXPECT_FALSE(client_info.Contains<String>(kP2PClientInfoGroupBSSIDProperty));
  EXPECT_FALSE(client_info.Contains<Integer>(kP2PClientInfoFrequencyProperty));
  EXPECT_FALSE(client_info.Contains<String>(kP2PClientInfoPassphraseProperty));

  EXPECT_EQ(client_info.Get<Integer>(kP2PClientInfoShillIDProperty), kShillId);
  EXPECT_EQ(client_info.Get<String>(kP2PClientInfoStateProperty),
            kP2PClientInfoStateIdle);

  // Initiate group connection.
  auto service = std::make_unique<MockP2PService>(
      device, kP2PSSID, kP2PPassphrase, kP2PFrequency);
  EXPECT_TRUE(device->Connect(std::move(service)));
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kClientAssociating);

  client_info = device->GetClientInfo();
  EXPECT_TRUE(client_info.Contains<Integer>(kP2PClientInfoShillIDProperty));
  EXPECT_TRUE(client_info.Contains<String>(kP2PClientInfoStateProperty));
  EXPECT_FALSE(client_info.Contains<String>(kP2PClientInfoSSIDProperty));
  EXPECT_FALSE(client_info.Contains<String>(kP2PClientInfoGroupBSSIDProperty));
  EXPECT_FALSE(client_info.Contains<Integer>(kP2PClientInfoFrequencyProperty));
  EXPECT_FALSE(client_info.Contains<String>(kP2PClientInfoPassphraseProperty));

  EXPECT_EQ(client_info.Get<Integer>(kP2PClientInfoShillIDProperty), kShillId);
  EXPECT_EQ(client_info.Get<String>(kP2PClientInfoStateProperty),
            kP2PClientInfoStateAssociating);

  // Emulate GroupStarted signal from wpa_supplicant.
  device->GroupStarted(DefaultGroupStartedProperties());
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kClientConfiguring);

  client_info = device->GetClientInfo();
  EXPECT_TRUE(client_info.Contains<Integer>(kP2PClientInfoShillIDProperty));
  EXPECT_TRUE(client_info.Contains<String>(kP2PClientInfoStateProperty));
  EXPECT_TRUE(client_info.Contains<String>(kP2PClientInfoSSIDProperty));
  EXPECT_TRUE(client_info.Contains<String>(kP2PClientInfoGroupBSSIDProperty));
  EXPECT_TRUE(client_info.Contains<Integer>(kP2PClientInfoFrequencyProperty));
  EXPECT_TRUE(client_info.Contains<String>(kP2PClientInfoPassphraseProperty));

  EXPECT_EQ(client_info.Get<Integer>(kP2PClientInfoShillIDProperty), kShillId);
  EXPECT_EQ(client_info.Get<String>(kP2PClientInfoStateProperty),
            kP2PClientInfoStateConfiguring);
  EXPECT_EQ(client_info.Get<String>(kP2PClientInfoSSIDProperty), kP2PSSID);
  EXPECT_EQ(client_info.Get<String>(kP2PClientInfoGroupBSSIDProperty),
            kP2PBSSID);
  EXPECT_EQ(client_info.Get<Integer>(kP2PClientInfoFrequencyProperty),
            kP2PFrequency);
  EXPECT_EQ(client_info.Get<String>(kP2PClientInfoPassphraseProperty),
            kP2PPassphrase);

  // Emulate network layer initialization.
  device->SetState(P2PDevice::P2PDeviceState::kClientConnected);

  client_info = device->GetClientInfo();
  EXPECT_TRUE(client_info.Contains<Integer>(kP2PClientInfoShillIDProperty));
  EXPECT_TRUE(client_info.Contains<String>(kP2PClientInfoStateProperty));
  EXPECT_TRUE(client_info.Contains<String>(kP2PClientInfoSSIDProperty));
  EXPECT_TRUE(client_info.Contains<String>(kP2PClientInfoGroupBSSIDProperty));
  EXPECT_TRUE(client_info.Contains<Integer>(kP2PClientInfoFrequencyProperty));
  EXPECT_TRUE(client_info.Contains<String>(kP2PClientInfoPassphraseProperty));

  EXPECT_EQ(client_info.Get<Integer>(kP2PClientInfoShillIDProperty), kShillId);
  EXPECT_EQ(client_info.Get<String>(kP2PClientInfoStateProperty),
            kP2PClientInfoStateConnected);
  EXPECT_EQ(client_info.Get<String>(kP2PClientInfoSSIDProperty), kP2PSSID);
  EXPECT_EQ(client_info.Get<String>(kP2PClientInfoGroupBSSIDProperty),
            kP2PBSSID);
  EXPECT_EQ(client_info.Get<Integer>(kP2PClientInfoFrequencyProperty),
            kP2PFrequency);
  EXPECT_EQ(client_info.Get<String>(kP2PClientInfoPassphraseProperty),
            kP2PPassphrase);

  // Disconnect group.
  EXPECT_TRUE(device->Disconnect());
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kClientDisconnecting);

  client_info = device->GetClientInfo();
  EXPECT_TRUE(client_info.Contains<Integer>(kP2PClientInfoShillIDProperty));
  EXPECT_TRUE(client_info.Contains<String>(kP2PClientInfoStateProperty));
  EXPECT_TRUE(client_info.Contains<String>(kP2PClientInfoSSIDProperty));
  EXPECT_TRUE(client_info.Contains<String>(kP2PClientInfoGroupBSSIDProperty));
  EXPECT_TRUE(client_info.Contains<Integer>(kP2PClientInfoFrequencyProperty));
  EXPECT_TRUE(client_info.Contains<String>(kP2PClientInfoPassphraseProperty));

  EXPECT_EQ(client_info.Get<Integer>(kP2PClientInfoShillIDProperty), kShillId);
  EXPECT_EQ(client_info.Get<String>(kP2PClientInfoStateProperty),
            kP2PClientInfoStateDisconnecting);
  EXPECT_EQ(client_info.Get<String>(kP2PClientInfoSSIDProperty), kP2PSSID);
  EXPECT_EQ(client_info.Get<String>(kP2PClientInfoGroupBSSIDProperty),
            kP2PBSSID);
  EXPECT_EQ(client_info.Get<Integer>(kP2PClientInfoFrequencyProperty),
            kP2PFrequency);
  EXPECT_EQ(client_info.Get<String>(kP2PClientInfoPassphraseProperty),
            kP2PPassphrase);

  // Emulate GroupFinished signal from wpa_supplicant
  device->GroupFinished(DefaultGroupFinishedProperties());
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kReady);

  client_info = device->GetClientInfo();
  EXPECT_TRUE(client_info.Contains<Integer>(kP2PClientInfoShillIDProperty));
  EXPECT_TRUE(client_info.Contains<String>(kP2PClientInfoStateProperty));
  EXPECT_FALSE(client_info.Contains<String>(kP2PClientInfoSSIDProperty));
  EXPECT_FALSE(client_info.Contains<String>(kP2PClientInfoGroupBSSIDProperty));
  EXPECT_FALSE(client_info.Contains<Integer>(kP2PClientInfoFrequencyProperty));
  EXPECT_FALSE(client_info.Contains<String>(kP2PClientInfoPassphraseProperty));

  EXPECT_EQ(client_info.Get<Integer>(kP2PClientInfoShillIDProperty), kShillId);
  EXPECT_EQ(client_info.Get<String>(kP2PClientInfoStateProperty),
            kP2PClientInfoStateIdle);

  // Stop device
  EXPECT_TRUE(device->Stop());
  EXPECT_EQ(device->state_, P2PDevice::P2PDeviceState::kUninitialized);
}

TEST_F(P2PDeviceTest, ClientInfo_EmptyOnGO) {
  scoped_refptr<P2PDevice> device =
      new P2PDevice(&manager_, LocalDevice::IfaceType::kP2PGO,
                    kPrimaryInterfaceName, kPhyIndex, kShillId, cb.Get());
  KeyValueStore client_info = device->GetClientInfo();
  EXPECT_TRUE(client_info.IsEmpty());
}

TEST_F(P2PDeviceTest, PeerJoinAndDisconnect) {
  scoped_refptr<P2PDevice> device =
      new P2PDevice(&manager_, LocalDevice::IfaceType::kP2PGO,
                    kPrimaryInterfaceName, kPhyIndex, kShillId, cb.Get());
  int num_of_peers = 10;

  // Emulate network layer initialization.
  device->SetState(P2PDevice::P2PDeviceState::kGOActive);

  // Emulate PeerJoined signals from wpa_supplicant.
  EXPECT_CALL(control_interface_, CreateSupplicantPeerProxy(_))
      .Times(num_of_peers);
  for (int peer_id = 0; peer_id < num_of_peers; peer_id++) {
    auto peer_proxy = std::make_unique<MockSupplicantPeerProxy>();
    auto peer_properties = DefaultPeerProperties(peer_id);
    auto peer_path = DefaultPeerObjectPath(peer_id);

    EXPECT_CALL(*peer_proxy, GetProperties(_))
        .WillOnce(DoAll(SetArgPointee<0>(peer_properties), Return(true)));
    ON_CALL(control_interface_, CreateSupplicantPeerProxy(_))
        .WillByDefault(Return(ByMove(std::move(peer_proxy))));
    device->PeerJoined(peer_path);

    EXPECT_TRUE(base::Contains(device->group_peers_, peer_path));
    EXPECT_EQ(device->group_peers_[peer_path], peer_properties);
    EXPECT_EQ(device->group_peers_.size(), peer_id + 1);
  }

  // Emulate PeerJoined duplicate signals from wpa_supplicant.
  EXPECT_CALL(control_interface_, CreateSupplicantPeerProxy(_)).Times(0);
  for (int peer_id = 0; peer_id < num_of_peers; peer_id++) {
    device->PeerJoined(DefaultPeerObjectPath(peer_id));
    EXPECT_EQ(device->group_peers_.size(), num_of_peers);
  }

  // Emulate PeerDisconnected signals from wpa_supplicant.
  for (int peer_id = 0; peer_id < num_of_peers; peer_id++) {
    auto peer_path = DefaultPeerObjectPath(peer_id);

    device->PeerDisconnected(peer_path);

    EXPECT_FALSE(base::Contains(device->group_peers_, peer_path));
    EXPECT_EQ(device->group_peers_.size(), num_of_peers - peer_id - 1);
  }
}

TEST_F(P2PDeviceTest, PeerJoinAndDisconnect_WhileNotReady) {
  scoped_refptr<P2PDevice> device =
      new P2PDevice(&manager_, LocalDevice::IfaceType::kP2PGO,
                    kPrimaryInterfaceName, kPhyIndex, kShillId, cb.Get());
  int num_of_peers = 10;

  // Emulate PeerJoined signals from wpa_supplicant.
  EXPECT_CALL(control_interface_, CreateSupplicantPeerProxy(_)).Times(0);
  for (int peer_id = 0; peer_id < num_of_peers; peer_id++) {
    device->PeerJoined(DefaultPeerObjectPath(peer_id));
    EXPECT_EQ(device->group_peers_.size(), 0);
  }

  // Emulate PeerDisconnected signals from wpa_supplicant.
  for (int peer_id = 0; peer_id < num_of_peers; peer_id++) {
    device->PeerDisconnected(DefaultPeerObjectPath(peer_id));
    EXPECT_EQ(device->group_peers_.size(), 0);
  }
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
