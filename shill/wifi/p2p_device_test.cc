// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/wifi/p2p_device.h"

#include <sys/socket.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/files/scoped_file.h>
#include <base/test/mock_callback.h>
#include <chromeos/net-base/byte_utils.h>
#include <chromeos/net-base/ipv4_address.h>
#include <chromeos/net-base/mac_address.h>
#include <chromeos/patchpanel/dbus/fake_client.h>

#include "shill/mock_control.h"
#include "shill/mock_manager.h"
#include "shill/mock_metrics.h"
#include "shill/network/mock_network.h"
#include "shill/supplicant/mock_supplicant_group_proxy.h"
#include "shill/supplicant/mock_supplicant_interface_proxy.h"
#include "shill/supplicant/mock_supplicant_p2pdevice_proxy.h"
#include "shill/supplicant/mock_supplicant_peer_proxy.h"
#include "shill/supplicant/wpa_supplicant.h"
#include "shill/technology.h"
#include "shill/test_event_dispatcher.h"
#include "shill/wifi/local_device.h"
#include "shill/wifi/mock_p2p_manager.h"
#include "shill/wifi/mock_p2p_service.h"
#include "shill/wifi/mock_wifi_phy.h"
#include "shill/wifi/mock_wifi_provider.h"

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
const int kInterfaceIdx = 1;
const RpcIdentifier kInterfacePath = RpcIdentifier("/interface/p2p-wlan0-0");
const RpcIdentifier kGroupPath =
    RpcIdentifier("/interface/p2p-wlan0-0/Groups/xx");
const uint32_t kPhyIndex = 5678;
const uint32_t kShillId = 0;
const char kP2PSSID[] = "chromeOS-1234";
const net_base::MacAddress kP2PBSSID(0xde, 0xad, 0xbe, 0xef, 0x00, 0x00);
const std::vector<uint8_t> kP2PMACAddress{0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a};
const char kP2PPassphrase[] = "test0000";
const int32_t kP2PFrequency = 2437;
const int kClientNetworkID = 10;
const int kLocalOnlyNetworkId = 733;
const WiFiPhy::Priority kPriority = WiFiPhy::Priority(0);

class MockPatchpanelClient : public patchpanel::FakeClient {
 public:
  MockPatchpanelClient() = default;
  ~MockPatchpanelClient() override = default;

  MOCK_METHOD(bool,
              CreateLocalOnlyNetwork,
              (const std::string&,
               patchpanel::Client::CreateLocalOnlyNetworkCallback),
              (override));
};

base::ScopedFD MakeFd() {
  return base::ScopedFD(socket(AF_INET, SOCK_DGRAM, 0));
}
}  // namespace

class P2PDeviceTest : public testing::Test {
 public:
  P2PDeviceTest()
      : manager_(&control_interface_, &dispatcher_, &metrics_),
        wifi_provider_(new NiceMock<MockWiFiProvider>(&manager_)),
        wifi_phy_(kPhyIndex),
        p2p_manager_(new NiceMock<MockP2PManager>(&manager_)),
        go_device_(new P2PDevice(&manager_,
                                 LocalDevice::IfaceType::kP2PGO,
                                 kPrimaryInterfaceName,
                                 kPhyIndex,
                                 kShillId,
                                 kPriority,
                                 cb.Get())),
        client_device_(new P2PDevice(&manager_,
                                     LocalDevice::IfaceType::kP2PClient,
                                     kPrimaryInterfaceName,
                                     kPhyIndex,
                                     kShillId,
                                     kPriority,
                                     cb.Get())),
        supplicant_primary_p2pdevice_proxy_(
            new NiceMock<MockSupplicantP2PDeviceProxy>()),
        supplicant_p2pdevice_proxy_(
            new NiceMock<MockSupplicantP2PDeviceProxy>()),
        supplicant_group_proxy_(new NiceMock<MockSupplicantGroupProxy>()),
        supplicant_interface_proxy_(
            new NiceMock<MockSupplicantInterfaceProxy>()) {
    // Replace the Manager's patchpanel DBus client with a mock.
    auto patchpanel = std::make_unique<MockPatchpanelClient>();
    patchpanel_ = patchpanel.get();
    manager_.set_patchpanel_client_for_testing(std::move(patchpanel));
    // Replace the WiFi provider's P2PManager with a mock.
    wifi_provider_->p2p_manager_.reset(p2p_manager_);
    // Replace the Manager's WiFi provider with a mock.
    manager_.wifi_provider_.reset(wifi_provider_);
    // Update the Manager's map from technology to provider.
    manager_.UpdateProviderMapping();
    ON_CALL(*patchpanel_, CreateLocalOnlyNetwork(kInterfaceName, _))
        .WillByDefault(Return(true));
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
        .WillByDefault(
            DoAll(SetArgPointee<0>(kP2PBSSID.ToBytes()), Return(true)));
    ON_CALL(*supplicant_group_proxy_, GetFrequency(_))
        .WillByDefault(DoAll(SetArgPointee<0>(kP2PFrequency), Return(true)));
    ON_CALL(*supplicant_group_proxy_, GetPassphrase(_))
        .WillByDefault(DoAll(SetArgPointee<0>(kP2PPassphrase), Return(true)));
    ON_CALL(*supplicant_interface_proxy_, GetIfname(_))
        .WillByDefault(DoAll(SetArgPointee<0>(kInterfaceName), Return(true)));
    ON_CALL(*supplicant_interface_proxy_, GetMACAddress(_))
        .WillByDefault(DoAll(SetArgPointee<0>(kP2PMACAddress), Return(true)));
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
    auto network = std::make_unique<NiceMock<MockNetwork>>(
        kInterfaceIdx, kInterfaceName, Technology::kWiFi);
    network_ = network.get();
    client_device_->client_network_for_test_ = std::move(network);
    ON_CALL(*network_, Start(_)).WillByDefault(Return());
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

  void DispatchPendingEvents() { dispatcher_.DispatchPendingEvents(); }

 protected:
  StrictMock<base::MockRepeatingCallback<void(LocalDevice::DeviceEvent,
                                              const LocalDevice*)>>
      cb;

  NiceMock<MockControl> control_interface_;
  EventDispatcherForTest dispatcher_;
  NiceMock<MockMetrics> metrics_;
  NiceMock<MockManager> manager_;
  MockPatchpanelClient* patchpanel_;
  MockWiFiProvider* wifi_provider_;
  MockWiFiPhy wifi_phy_;
  MockP2PManager* p2p_manager_;
  scoped_refptr<P2PDevice> go_device_;
  scoped_refptr<P2PDevice> client_device_;
  MockNetwork* network_;  // owned by |client_device_|
  std::unique_ptr<MockSupplicantP2PDeviceProxy>
      supplicant_primary_p2pdevice_proxy_;
  MockSupplicantP2PDeviceProxy* supplicant_p2pdevice_proxy_;
  MockSupplicantGroupProxy* supplicant_group_proxy_;
  MockSupplicantInterfaceProxy* supplicant_interface_proxy_;
};

TEST_F(P2PDeviceTest, DeviceOnOff) {
  CHECK_EQ(go_device_->state_, P2PDevice::P2PDeviceState::kUninitialized);
  go_device_->Start();
  CHECK_EQ(go_device_->state_, P2PDevice::P2PDeviceState::kReady);
  go_device_->Stop();
  CHECK_EQ(go_device_->state_, P2PDevice::P2PDeviceState::kUninitialized);
}

TEST_F(P2PDeviceTest, GroupInfo) {
  // kReady
  EXPECT_TRUE(go_device_->Start());
  EXPECT_EQ(go_device_->state_, P2PDevice::P2PDeviceState::kReady);

  KeyValueStore group_info = go_device_->GetGroupInfo();
  EXPECT_TRUE(group_info.Contains<int32_t>(kP2PGroupInfoShillIDProperty));
  EXPECT_TRUE(group_info.Contains<String>(kP2PGroupInfoStateProperty));
  EXPECT_FALSE(group_info.Contains<String>(kP2PGroupInfoSSIDProperty));
  EXPECT_FALSE(group_info.Contains<String>(kP2PGroupInfoBSSIDProperty));
  EXPECT_FALSE(group_info.Contains<Integer>(kP2PGroupInfoFrequencyProperty));
  EXPECT_FALSE(group_info.Contains<String>(kP2PGroupInfoPassphraseProperty));
  EXPECT_FALSE(group_info.Contains<String>(kP2PGroupInfoInterfaceProperty));
  EXPECT_FALSE(group_info.Contains<String>(kP2PGroupInfoMACAddressProperty));
  EXPECT_FALSE(group_info.Contains<String>(kP2PGroupInfoIPv4AddressProperty));
  EXPECT_FALSE(group_info.Contains<String>(kP2PGroupInfoIPv6AddressProperty));
  EXPECT_FALSE(group_info.Contains<int32_t>(kP2PGroupInfoNetworkIDProperty));
  EXPECT_FALSE(group_info.Contains<Stringmaps>(kP2PGroupInfoClientsProperty));

  EXPECT_EQ(group_info.Get<int32_t>(kP2PGroupInfoShillIDProperty), kShillId);
  EXPECT_EQ(group_info.Get<String>(kP2PGroupInfoStateProperty),
            kP2PGroupInfoStateIdle);

  // kGOStarting
  auto service = std::make_unique<MockP2PService>(
      go_device_, kP2PSSID, kP2PPassphrase, kP2PFrequency);
  EXPECT_TRUE(go_device_->CreateGroup(std::move(service)));
  EXPECT_EQ(go_device_->state_, P2PDevice::P2PDeviceState::kGOStarting);

  group_info = go_device_->GetGroupInfo();
  EXPECT_TRUE(group_info.Contains<int32_t>(kP2PGroupInfoShillIDProperty));
  EXPECT_TRUE(group_info.Contains<String>(kP2PGroupInfoStateProperty));
  EXPECT_FALSE(group_info.Contains<String>(kP2PGroupInfoSSIDProperty));
  EXPECT_FALSE(group_info.Contains<String>(kP2PGroupInfoBSSIDProperty));
  EXPECT_FALSE(group_info.Contains<Integer>(kP2PGroupInfoFrequencyProperty));
  EXPECT_FALSE(group_info.Contains<String>(kP2PGroupInfoPassphraseProperty));
  EXPECT_FALSE(group_info.Contains<String>(kP2PGroupInfoInterfaceProperty));
  EXPECT_FALSE(group_info.Contains<String>(kP2PGroupInfoMACAddressProperty));
  EXPECT_FALSE(group_info.Contains<String>(kP2PGroupInfoIPv4AddressProperty));
  EXPECT_FALSE(group_info.Contains<String>(kP2PGroupInfoIPv6AddressProperty));
  EXPECT_FALSE(group_info.Contains<int32_t>(kP2PGroupInfoNetworkIDProperty));
  EXPECT_FALSE(group_info.Contains<Stringmaps>(kP2PGroupInfoClientsProperty));

  EXPECT_EQ(group_info.Get<int32_t>(kP2PGroupInfoShillIDProperty), kShillId);
  EXPECT_EQ(group_info.Get<String>(kP2PGroupInfoStateProperty),
            kP2PGroupInfoStateStarting);

  // kGOConfiguring
  EXPECT_CALL(cb, Run(LocalDevice::DeviceEvent::kLinkUp, _)).Times(1);
  go_device_->GroupStarted(DefaultGroupStartedProperties());
  EXPECT_EQ(go_device_->state_, P2PDevice::P2PDeviceState::kGOConfiguring);

  group_info = go_device_->GetGroupInfo();
  EXPECT_TRUE(group_info.Contains<int32_t>(kP2PGroupInfoShillIDProperty));
  EXPECT_TRUE(group_info.Contains<String>(kP2PGroupInfoStateProperty));
  EXPECT_TRUE(group_info.Contains<String>(kP2PGroupInfoSSIDProperty));
  EXPECT_TRUE(group_info.Contains<String>(kP2PGroupInfoBSSIDProperty));
  EXPECT_TRUE(group_info.Contains<Integer>(kP2PGroupInfoFrequencyProperty));
  EXPECT_TRUE(group_info.Contains<String>(kP2PGroupInfoPassphraseProperty));
  EXPECT_TRUE(group_info.Contains<String>(kP2PGroupInfoInterfaceProperty));
  EXPECT_TRUE(group_info.Contains<String>(kP2PGroupInfoMACAddressProperty));
  EXPECT_FALSE(group_info.Contains<String>(kP2PGroupInfoIPv4AddressProperty));
  EXPECT_FALSE(group_info.Contains<String>(kP2PGroupInfoIPv6AddressProperty));
  EXPECT_FALSE(group_info.Contains<int32_t>(kP2PGroupInfoNetworkIDProperty));
  EXPECT_TRUE(group_info.Contains<Stringmaps>(kP2PGroupInfoClientsProperty));

  EXPECT_EQ(group_info.Get<int32_t>(kP2PGroupInfoShillIDProperty), kShillId);
  EXPECT_EQ(group_info.Get<String>(kP2PGroupInfoStateProperty),
            kP2PGroupInfoStateConfiguring);
  EXPECT_EQ(group_info.Get<String>(kP2PGroupInfoSSIDProperty), kP2PSSID);
  EXPECT_EQ(group_info.Get<String>(kP2PGroupInfoBSSIDProperty),
            kP2PBSSID.ToString());
  EXPECT_EQ(group_info.Get<Integer>(kP2PGroupInfoFrequencyProperty),
            kP2PFrequency);
  EXPECT_EQ(group_info.Get<String>(kP2PGroupInfoPassphraseProperty),
            kP2PPassphrase);
  EXPECT_EQ(group_info.Get<String>(kP2PGroupInfoInterfaceProperty),
            kInterfaceName);
  EXPECT_EQ(
      group_info.Get<String>(kP2PGroupInfoMACAddressProperty),
      net_base::MacAddress::CreateFromBytes(kP2PMACAddress).value().ToString());
  EXPECT_EQ(group_info.Get<Stringmaps>(kP2PGroupInfoClientsProperty).size(), 0);

  // kGOActive
  EXPECT_CALL(cb, Run(LocalDevice::DeviceEvent::kNetworkUp, _)).Times(1);
  patchpanel::Client::DownstreamNetwork downstream_network = {
      kLocalOnlyNetworkId, kInterfaceName,
      net_base::IPv4CIDR::CreateFromStringAndPrefix("192.168.1.128", 25)
          .value(),
      net_base::IPv4Address(192, 168, 1, 1)};
  go_device_->OnGroupNetworkStarted(MakeFd(), downstream_network);
  go_device_->UpdateGroupNetworkInfo(downstream_network);
  EXPECT_EQ(go_device_->state_, P2PDevice::P2PDeviceState::kGOActive);

  group_info = go_device_->GetGroupInfo();
  EXPECT_TRUE(group_info.Contains<int32_t>(kP2PGroupInfoShillIDProperty));
  EXPECT_TRUE(group_info.Contains<String>(kP2PGroupInfoStateProperty));
  EXPECT_TRUE(group_info.Contains<String>(kP2PGroupInfoSSIDProperty));
  EXPECT_TRUE(group_info.Contains<String>(kP2PGroupInfoBSSIDProperty));
  EXPECT_TRUE(group_info.Contains<Integer>(kP2PGroupInfoFrequencyProperty));
  EXPECT_TRUE(group_info.Contains<String>(kP2PGroupInfoPassphraseProperty));
  EXPECT_TRUE(group_info.Contains<String>(kP2PGroupInfoInterfaceProperty));
  EXPECT_TRUE(group_info.Contains<String>(kP2PGroupInfoMACAddressProperty));
  EXPECT_TRUE(group_info.Contains<String>(kP2PGroupInfoIPv4AddressProperty));
  EXPECT_FALSE(group_info.Contains<String>(kP2PGroupInfoIPv6AddressProperty));
  EXPECT_TRUE(group_info.Contains<int32_t>(kP2PGroupInfoNetworkIDProperty));
  EXPECT_TRUE(group_info.Contains<Stringmaps>(kP2PGroupInfoClientsProperty));

  EXPECT_EQ(group_info.Get<int32_t>(kP2PGroupInfoShillIDProperty), kShillId);
  EXPECT_EQ(group_info.Get<String>(kP2PGroupInfoStateProperty),
            kP2PGroupInfoStateActive);
  EXPECT_EQ(group_info.Get<String>(kP2PGroupInfoSSIDProperty), kP2PSSID);
  EXPECT_EQ(group_info.Get<String>(kP2PGroupInfoBSSIDProperty),
            kP2PBSSID.ToString());
  EXPECT_EQ(group_info.Get<Integer>(kP2PGroupInfoFrequencyProperty),
            kP2PFrequency);
  EXPECT_EQ(group_info.Get<String>(kP2PGroupInfoPassphraseProperty),
            kP2PPassphrase);
  EXPECT_EQ(group_info.Get<String>(kP2PGroupInfoInterfaceProperty),
            kInterfaceName);
  EXPECT_EQ(
      group_info.Get<String>(kP2PGroupInfoMACAddressProperty),
      net_base::MacAddress::CreateFromBytes(kP2PMACAddress).value().ToString());
  EXPECT_EQ(group_info.Get<int32_t>(kP2PGroupInfoNetworkIDProperty),
            kLocalOnlyNetworkId);
  EXPECT_EQ(group_info.Get<String>(kP2PGroupInfoIPv4AddressProperty),
            "192.168.1.1");
  EXPECT_EQ(group_info.Get<Stringmaps>(kP2PGroupInfoClientsProperty).size(), 0);

  // Emulate PeerJoined signals from wpa_supplicant.
  int num_of_peers = 10;
  EXPECT_CALL(cb, Run(LocalDevice::DeviceEvent::kPeerConnected, _))
      .Times(num_of_peers);
  EXPECT_CALL(control_interface_, CreateSupplicantPeerProxy(_))
      .Times(num_of_peers);
  for (int peer_id = 0; peer_id < num_of_peers; peer_id++) {
    auto peer_proxy = std::make_unique<MockSupplicantPeerProxy>();
    auto peer_path = DefaultPeerObjectPath(peer_id);

    ON_CALL(control_interface_, CreateSupplicantPeerProxy(_))
        .WillByDefault(Return(ByMove(std::move(peer_proxy))));
    go_device_->PeerJoined(peer_path);

    EXPECT_TRUE(base::Contains(go_device_->group_peers_, peer_path));
    EXPECT_EQ(go_device_->group_peers_.size(), peer_id + 1);
  }
  DispatchPendingEvents();

  group_info = go_device_->GetGroupInfo();
  EXPECT_TRUE(group_info.Contains<int32_t>(kP2PGroupInfoShillIDProperty));
  EXPECT_TRUE(group_info.Contains<String>(kP2PGroupInfoStateProperty));
  EXPECT_TRUE(group_info.Contains<String>(kP2PGroupInfoSSIDProperty));
  EXPECT_TRUE(group_info.Contains<String>(kP2PGroupInfoBSSIDProperty));
  EXPECT_TRUE(group_info.Contains<Integer>(kP2PGroupInfoFrequencyProperty));
  EXPECT_TRUE(group_info.Contains<String>(kP2PGroupInfoPassphraseProperty));
  EXPECT_TRUE(group_info.Contains<String>(kP2PGroupInfoInterfaceProperty));
  EXPECT_TRUE(group_info.Contains<String>(kP2PGroupInfoMACAddressProperty));
  EXPECT_TRUE(group_info.Contains<String>(kP2PGroupInfoIPv4AddressProperty));
  EXPECT_FALSE(group_info.Contains<String>(kP2PGroupInfoIPv6AddressProperty));
  EXPECT_TRUE(group_info.Contains<int32_t>(kP2PGroupInfoNetworkIDProperty));
  EXPECT_TRUE(group_info.Contains<Stringmaps>(kP2PGroupInfoClientsProperty));

  EXPECT_EQ(group_info.Get<int32_t>(kP2PGroupInfoShillIDProperty), kShillId);
  EXPECT_EQ(group_info.Get<String>(kP2PGroupInfoStateProperty),
            kP2PGroupInfoStateActive);
  EXPECT_EQ(group_info.Get<String>(kP2PGroupInfoSSIDProperty), kP2PSSID);
  EXPECT_EQ(group_info.Get<String>(kP2PGroupInfoBSSIDProperty),
            kP2PBSSID.ToString());
  EXPECT_EQ(group_info.Get<Integer>(kP2PGroupInfoFrequencyProperty),
            kP2PFrequency);
  EXPECT_EQ(group_info.Get<String>(kP2PGroupInfoPassphraseProperty),
            kP2PPassphrase);
  EXPECT_EQ(group_info.Get<String>(kP2PGroupInfoInterfaceProperty),
            kInterfaceName);
  EXPECT_EQ(
      group_info.Get<String>(kP2PGroupInfoMACAddressProperty),
      net_base::MacAddress::CreateFromBytes(kP2PMACAddress).value().ToString());
  EXPECT_EQ(group_info.Get<int32_t>(kP2PGroupInfoNetworkIDProperty),
            kLocalOnlyNetworkId);
  EXPECT_EQ(group_info.Get<String>(kP2PGroupInfoIPv4AddressProperty),
            "192.168.1.1");
  EXPECT_EQ(group_info.Get<Stringmaps>(kP2PGroupInfoClientsProperty).size(),
            num_of_peers);
  auto group_clients = group_info.Get<Stringmaps>(kP2PGroupInfoClientsProperty);
  for (auto const& client : group_clients)
    EXPECT_TRUE(base::Contains(client, kP2PGroupInfoClientMACAddressProperty));

  // kGOStopping
  EXPECT_TRUE(go_device_->RemoveGroup());
  EXPECT_EQ(go_device_->state_, P2PDevice::P2PDeviceState::kGOStopping);

  group_info = go_device_->GetGroupInfo();
  EXPECT_TRUE(group_info.Contains<int32_t>(kP2PGroupInfoShillIDProperty));
  EXPECT_TRUE(group_info.Contains<String>(kP2PGroupInfoStateProperty));
  EXPECT_FALSE(group_info.Contains<String>(kP2PGroupInfoSSIDProperty));
  EXPECT_FALSE(group_info.Contains<String>(kP2PGroupInfoBSSIDProperty));
  EXPECT_FALSE(group_info.Contains<Integer>(kP2PGroupInfoFrequencyProperty));
  EXPECT_FALSE(group_info.Contains<String>(kP2PGroupInfoPassphraseProperty));
  EXPECT_FALSE(group_info.Contains<String>(kP2PGroupInfoInterfaceProperty));
  EXPECT_FALSE(group_info.Contains<String>(kP2PGroupInfoMACAddressProperty));
  EXPECT_FALSE(group_info.Contains<String>(kP2PGroupInfoIPv4AddressProperty));
  EXPECT_FALSE(group_info.Contains<String>(kP2PGroupInfoIPv6AddressProperty));
  EXPECT_FALSE(group_info.Contains<int32_t>(kP2PGroupInfoNetworkIDProperty));
  EXPECT_FALSE(group_info.Contains<Stringmaps>(kP2PGroupInfoClientsProperty));

  EXPECT_EQ(group_info.Get<int32_t>(kP2PGroupInfoShillIDProperty), kShillId);
  EXPECT_EQ(group_info.Get<String>(kP2PGroupInfoStateProperty),
            kP2PGroupInfoStateStopping);

  // kReady
  EXPECT_CALL(cb, Run(LocalDevice::DeviceEvent::kLinkDown, _)).Times(1);
  go_device_->GroupFinished(DefaultGroupFinishedProperties());
  EXPECT_EQ(go_device_->state_, P2PDevice::P2PDeviceState::kReady);
  DispatchPendingEvents();

  group_info = go_device_->GetGroupInfo();
  EXPECT_TRUE(group_info.Contains<int32_t>(kP2PGroupInfoShillIDProperty));
  EXPECT_TRUE(group_info.Contains<String>(kP2PGroupInfoStateProperty));
  EXPECT_FALSE(group_info.Contains<String>(kP2PGroupInfoSSIDProperty));
  EXPECT_FALSE(group_info.Contains<String>(kP2PGroupInfoBSSIDProperty));
  EXPECT_FALSE(group_info.Contains<Integer>(kP2PGroupInfoFrequencyProperty));
  EXPECT_FALSE(group_info.Contains<String>(kP2PGroupInfoPassphraseProperty));
  EXPECT_FALSE(group_info.Contains<String>(kP2PGroupInfoInterfaceProperty));
  EXPECT_FALSE(group_info.Contains<String>(kP2PGroupInfoIPv4AddressProperty));
  EXPECT_FALSE(group_info.Contains<String>(kP2PGroupInfoIPv6AddressProperty));
  EXPECT_FALSE(group_info.Contains<int32_t>(kP2PGroupInfoNetworkIDProperty));
  EXPECT_FALSE(group_info.Contains<Stringmaps>(kP2PGroupInfoClientsProperty));

  EXPECT_EQ(group_info.Get<int32_t>(kP2PGroupInfoShillIDProperty), kShillId);
  EXPECT_EQ(group_info.Get<String>(kP2PGroupInfoStateProperty),
            kP2PGroupInfoStateIdle);

  // Stop device
  EXPECT_TRUE(go_device_->Stop());
  EXPECT_EQ(go_device_->state_, P2PDevice::P2PDeviceState::kUninitialized);
}

TEST_F(P2PDeviceTest, GroupInfo_EmptyOnClient) {
  KeyValueStore group_info = client_device_->GetGroupInfo();
  EXPECT_TRUE(group_info.IsEmpty());
}

TEST_F(P2PDeviceTest, ClientInfo) {
  // kReady
  EXPECT_TRUE(client_device_->Start());
  EXPECT_EQ(client_device_->state_, P2PDevice::P2PDeviceState::kReady);

  KeyValueStore client_info = client_device_->GetClientInfo();
  EXPECT_TRUE(client_info.Contains<int32_t>(kP2PClientInfoShillIDProperty));
  EXPECT_TRUE(client_info.Contains<String>(kP2PClientInfoStateProperty));
  EXPECT_FALSE(client_info.Contains<String>(kP2PClientInfoSSIDProperty));
  EXPECT_FALSE(client_info.Contains<String>(kP2PClientInfoGroupBSSIDProperty));
  EXPECT_FALSE(client_info.Contains<Integer>(kP2PClientInfoFrequencyProperty));
  EXPECT_FALSE(client_info.Contains<String>(kP2PClientInfoPassphraseProperty));
  EXPECT_FALSE(client_info.Contains<String>(kP2PClientInfoInterfaceProperty));
  EXPECT_FALSE(client_info.Contains<String>(kP2PClientInfoMACAddressProperty));
  EXPECT_FALSE(client_info.Contains<String>(kP2PClientInfoIPv4AddressProperty));
  EXPECT_FALSE(client_info.Contains<String>(kP2PClientInfoIPv6AddressProperty));
  EXPECT_FALSE(client_info.Contains<int32_t>(kP2PClientInfoNetworkIDProperty));
  EXPECT_FALSE(
      client_info.Contains<Stringmap>(kP2PClientInfoGroupOwnerProperty));

  EXPECT_EQ(client_info.Get<int32_t>(kP2PClientInfoShillIDProperty), kShillId);
  EXPECT_EQ(client_info.Get<String>(kP2PClientInfoStateProperty),
            kP2PClientInfoStateIdle);

  // kClientAssociating
  auto service = std::make_unique<MockP2PService>(
      client_device_, kP2PSSID, kP2PPassphrase, kP2PFrequency);
  EXPECT_TRUE(client_device_->Connect(std::move(service)));
  EXPECT_EQ(client_device_->state_,
            P2PDevice::P2PDeviceState::kClientAssociating);

  client_info = client_device_->GetClientInfo();
  EXPECT_TRUE(client_info.Contains<int32_t>(kP2PClientInfoShillIDProperty));
  EXPECT_TRUE(client_info.Contains<String>(kP2PClientInfoStateProperty));
  EXPECT_FALSE(client_info.Contains<String>(kP2PClientInfoSSIDProperty));
  EXPECT_FALSE(client_info.Contains<String>(kP2PClientInfoGroupBSSIDProperty));
  EXPECT_FALSE(client_info.Contains<Integer>(kP2PClientInfoFrequencyProperty));
  EXPECT_FALSE(client_info.Contains<String>(kP2PClientInfoPassphraseProperty));
  EXPECT_FALSE(client_info.Contains<String>(kP2PClientInfoInterfaceProperty));
  EXPECT_FALSE(client_info.Contains<String>(kP2PClientInfoMACAddressProperty));
  EXPECT_FALSE(client_info.Contains<String>(kP2PClientInfoIPv4AddressProperty));
  EXPECT_FALSE(client_info.Contains<String>(kP2PClientInfoIPv6AddressProperty));
  EXPECT_FALSE(client_info.Contains<int32_t>(kP2PClientInfoNetworkIDProperty));
  EXPECT_FALSE(
      client_info.Contains<Stringmap>(kP2PClientInfoGroupOwnerProperty));

  EXPECT_EQ(client_info.Get<int32_t>(kP2PClientInfoShillIDProperty), kShillId);
  EXPECT_EQ(client_info.Get<String>(kP2PClientInfoStateProperty),
            kP2PClientInfoStateAssociating);

  // kClientConfiguring
  EXPECT_CALL(cb, Run(LocalDevice::DeviceEvent::kLinkUp, _)).Times(1);
  client_device_->GroupStarted(DefaultGroupStartedProperties());
  EXPECT_EQ(client_device_->state_,
            P2PDevice::P2PDeviceState::kClientConfiguring);

  client_info = client_device_->GetClientInfo();
  EXPECT_TRUE(client_info.Contains<int32_t>(kP2PClientInfoShillIDProperty));
  EXPECT_TRUE(client_info.Contains<String>(kP2PClientInfoStateProperty));
  EXPECT_TRUE(client_info.Contains<String>(kP2PClientInfoSSIDProperty));
  EXPECT_TRUE(client_info.Contains<String>(kP2PClientInfoGroupBSSIDProperty));
  EXPECT_TRUE(client_info.Contains<Integer>(kP2PClientInfoFrequencyProperty));
  EXPECT_TRUE(client_info.Contains<String>(kP2PClientInfoPassphraseProperty));
  EXPECT_TRUE(client_info.Contains<String>(kP2PClientInfoInterfaceProperty));
  EXPECT_TRUE(client_info.Contains<String>(kP2PClientInfoMACAddressProperty));
  EXPECT_FALSE(client_info.Contains<String>(kP2PClientInfoIPv4AddressProperty));
  EXPECT_FALSE(client_info.Contains<String>(kP2PClientInfoIPv6AddressProperty));
  EXPECT_FALSE(client_info.Contains<int32_t>(kP2PClientInfoNetworkIDProperty));
  EXPECT_TRUE(
      client_info.Contains<Stringmap>(kP2PClientInfoGroupOwnerProperty));

  EXPECT_EQ(client_info.Get<int32_t>(kP2PClientInfoShillIDProperty), kShillId);
  EXPECT_EQ(client_info.Get<String>(kP2PClientInfoSSIDProperty), kP2PSSID);
  EXPECT_EQ(client_info.Get<String>(kP2PClientInfoGroupBSSIDProperty),
            kP2PBSSID.ToString());
  EXPECT_EQ(client_info.Get<Integer>(kP2PClientInfoFrequencyProperty),
            kP2PFrequency);
  EXPECT_EQ(client_info.Get<String>(kP2PClientInfoPassphraseProperty),
            kP2PPassphrase);
  EXPECT_EQ(client_info.Get<String>(kP2PClientInfoInterfaceProperty),
            kInterfaceName);
  EXPECT_EQ(
      client_info.Get<String>(kP2PClientInfoMACAddressProperty),
      net_base::MacAddress::CreateFromBytes(kP2PMACAddress).value().ToString());
  auto GOInfo = client_info.Get<Stringmap>(kP2PClientInfoGroupOwnerProperty);
  EXPECT_EQ(GOInfo.at(kP2PClientInfoGroupOwnerMACAddressProperty),
            kP2PBSSID.ToString());
  EXPECT_EQ(GOInfo.count(kP2PClientInfoGroupOwnerIPv4AddressProperty), 0);
  EXPECT_EQ(GOInfo.count(kP2PClientInfoGroupOwnerIPv6AddressProperty), 0);

  // kClientConnected
  EXPECT_CALL(cb, Run(LocalDevice::DeviceEvent::kNetworkUp, _)).Times(1);
  client_device_->OnConnectionUpdated(kInterfaceIdx);
  EXPECT_EQ(client_device_->state_,
            P2PDevice::P2PDeviceState::kClientConnected);

  net_base::NetworkConfig config;
  config.ipv4_address =
      *net_base::IPv4CIDR::CreateFromCIDRString("192.168.1.100/24");
  config.ipv4_gateway = *net_base::IPv4Address::CreateFromString("192.168.1.1");
  network_->set_dhcp_network_config_for_testing(config);
  ON_CALL(*network_, network_id()).WillByDefault(Return(kClientNetworkID));

  client_info = client_device_->GetClientInfo();
  EXPECT_TRUE(client_info.Contains<int32_t>(kP2PClientInfoShillIDProperty));
  EXPECT_TRUE(client_info.Contains<String>(kP2PClientInfoStateProperty));
  EXPECT_TRUE(client_info.Contains<String>(kP2PClientInfoSSIDProperty));
  EXPECT_TRUE(client_info.Contains<String>(kP2PClientInfoGroupBSSIDProperty));
  EXPECT_TRUE(client_info.Contains<Integer>(kP2PClientInfoFrequencyProperty));
  EXPECT_TRUE(client_info.Contains<String>(kP2PClientInfoPassphraseProperty));
  EXPECT_TRUE(client_info.Contains<String>(kP2PClientInfoInterfaceProperty));
  EXPECT_TRUE(client_info.Contains<String>(kP2PClientInfoMACAddressProperty));
  EXPECT_TRUE(client_info.Contains<String>(kP2PClientInfoIPv4AddressProperty));
  EXPECT_FALSE(client_info.Contains<String>(kP2PClientInfoIPv6AddressProperty));
  EXPECT_TRUE(client_info.Contains<int32_t>(kP2PClientInfoNetworkIDProperty));
  EXPECT_TRUE(
      client_info.Contains<Stringmap>(kP2PClientInfoGroupOwnerProperty));

  EXPECT_EQ(client_info.Get<int32_t>(kP2PClientInfoShillIDProperty), kShillId);
  EXPECT_EQ(client_info.Get<String>(kP2PClientInfoSSIDProperty), kP2PSSID);
  EXPECT_EQ(client_info.Get<String>(kP2PClientInfoGroupBSSIDProperty),
            kP2PBSSID.ToString());
  EXPECT_EQ(client_info.Get<Integer>(kP2PClientInfoFrequencyProperty),
            kP2PFrequency);
  EXPECT_EQ(client_info.Get<String>(kP2PClientInfoPassphraseProperty),
            kP2PPassphrase);
  EXPECT_EQ(client_info.Get<String>(kP2PClientInfoInterfaceProperty),
            kInterfaceName);
  EXPECT_EQ(
      client_info.Get<String>(kP2PClientInfoMACAddressProperty),
      net_base::MacAddress::CreateFromBytes(kP2PMACAddress).value().ToString());
  EXPECT_EQ(client_info.Get<String>(kP2PClientInfoIPv4AddressProperty),
            "192.168.1.100");
  EXPECT_EQ(client_info.Get<int32_t>(kP2PClientInfoNetworkIDProperty),
            kClientNetworkID);
  GOInfo = client_info.Get<Stringmap>(kP2PClientInfoGroupOwnerProperty);
  EXPECT_EQ(GOInfo.at(kP2PClientInfoGroupOwnerMACAddressProperty),
            kP2PBSSID.ToString());
  EXPECT_EQ(GOInfo.at(kP2PClientInfoGroupOwnerIPv4AddressProperty),
            "192.168.1.1");
  EXPECT_EQ(GOInfo.count(kP2PClientInfoGroupOwnerIPv6AddressProperty), 0);

  // Disconnect group.
  EXPECT_TRUE(client_device_->Disconnect());
  EXPECT_EQ(client_device_->state_,
            P2PDevice::P2PDeviceState::kClientDisconnecting);

  client_info = client_device_->GetClientInfo();
  EXPECT_TRUE(client_info.Contains<int32_t>(kP2PClientInfoShillIDProperty));
  EXPECT_TRUE(client_info.Contains<String>(kP2PClientInfoStateProperty));
  EXPECT_FALSE(client_info.Contains<String>(kP2PClientInfoSSIDProperty));
  EXPECT_FALSE(client_info.Contains<String>(kP2PClientInfoGroupBSSIDProperty));
  EXPECT_FALSE(client_info.Contains<Integer>(kP2PClientInfoFrequencyProperty));
  EXPECT_FALSE(client_info.Contains<String>(kP2PClientInfoPassphraseProperty));
  EXPECT_FALSE(client_info.Contains<String>(kP2PClientInfoInterfaceProperty));
  EXPECT_FALSE(client_info.Contains<String>(kP2PClientInfoMACAddressProperty));
  EXPECT_FALSE(client_info.Contains<String>(kP2PClientInfoIPv4AddressProperty));
  EXPECT_FALSE(client_info.Contains<String>(kP2PClientInfoIPv6AddressProperty));
  EXPECT_FALSE(client_info.Contains<int32_t>(kP2PClientInfoNetworkIDProperty));
  EXPECT_FALSE(
      client_info.Contains<Stringmap>(kP2PClientInfoGroupOwnerProperty));

  EXPECT_EQ(client_info.Get<int32_t>(kP2PClientInfoShillIDProperty), kShillId);
  EXPECT_EQ(client_info.Get<String>(kP2PClientInfoStateProperty),
            kP2PClientInfoStateDisconnecting);

  // Emulate GroupFinished signal from wpa_supplicant
  EXPECT_CALL(cb, Run(LocalDevice::DeviceEvent::kLinkDown, _)).Times(1);
  client_device_->GroupFinished(DefaultGroupFinishedProperties());
  EXPECT_EQ(client_device_->state_, P2PDevice::P2PDeviceState::kReady);
  DispatchPendingEvents();

  client_info = client_device_->GetClientInfo();
  EXPECT_TRUE(client_info.Contains<int32_t>(kP2PClientInfoShillIDProperty));
  EXPECT_TRUE(client_info.Contains<String>(kP2PClientInfoStateProperty));
  EXPECT_FALSE(client_info.Contains<String>(kP2PClientInfoSSIDProperty));
  EXPECT_FALSE(client_info.Contains<String>(kP2PClientInfoGroupBSSIDProperty));
  EXPECT_FALSE(client_info.Contains<Integer>(kP2PClientInfoFrequencyProperty));
  EXPECT_FALSE(client_info.Contains<String>(kP2PClientInfoPassphraseProperty));
  EXPECT_FALSE(client_info.Contains<String>(kP2PClientInfoInterfaceProperty));
  EXPECT_FALSE(client_info.Contains<String>(kP2PClientInfoMACAddressProperty));
  EXPECT_FALSE(client_info.Contains<String>(kP2PClientInfoIPv4AddressProperty));
  EXPECT_FALSE(client_info.Contains<String>(kP2PClientInfoIPv6AddressProperty));
  EXPECT_FALSE(client_info.Contains<int32_t>(kP2PClientInfoNetworkIDProperty));
  EXPECT_FALSE(
      client_info.Contains<Stringmap>(kP2PClientInfoGroupOwnerProperty));

  EXPECT_EQ(client_info.Get<int32_t>(kP2PClientInfoShillIDProperty), kShillId);
  EXPECT_EQ(client_info.Get<String>(kP2PClientInfoStateProperty),
            kP2PClientInfoStateIdle);

  // Stop device
  EXPECT_TRUE(client_device_->Stop());
  EXPECT_EQ(client_device_->state_, P2PDevice::P2PDeviceState::kUninitialized);
}

TEST_F(P2PDeviceTest, ClientInfo_EmptyOnGO) {
  KeyValueStore client_info = go_device_->GetClientInfo();
  EXPECT_TRUE(client_info.IsEmpty());
}

TEST_F(P2PDeviceTest, PeerJoinAndDisconnect) {
  int num_of_peers = 10;

  // Emulate network layer initialization.
  go_device_->SetState(P2PDevice::P2PDeviceState::kGOActive);

  // Emulate PeerJoined signals from wpa_supplicant.
  EXPECT_CALL(cb, Run(LocalDevice::DeviceEvent::kPeerConnected, _))
      .Times(num_of_peers);
  EXPECT_CALL(control_interface_, CreateSupplicantPeerProxy(_))
      .Times(num_of_peers);
  for (int peer_id = 0; peer_id < num_of_peers; peer_id++) {
    auto peer_proxy = std::make_unique<MockSupplicantPeerProxy>();
    auto peer_path = DefaultPeerObjectPath(peer_id);

    ON_CALL(control_interface_, CreateSupplicantPeerProxy(_))
        .WillByDefault(Return(ByMove(std::move(peer_proxy))));
    go_device_->PeerJoined(peer_path);

    EXPECT_TRUE(base::Contains(go_device_->group_peers_, peer_path));
    EXPECT_EQ(go_device_->group_peers_.size(), peer_id + 1);
  }
  DispatchPendingEvents();

  // Emulate PeerJoined duplicate signals from wpa_supplicant.
  EXPECT_CALL(cb, Run(LocalDevice::DeviceEvent::kPeerConnected, _)).Times(0);
  EXPECT_CALL(control_interface_, CreateSupplicantPeerProxy(_)).Times(0);
  for (int peer_id = 0; peer_id < num_of_peers; peer_id++) {
    go_device_->PeerJoined(DefaultPeerObjectPath(peer_id));
    EXPECT_EQ(go_device_->group_peers_.size(), num_of_peers);
  }
  DispatchPendingEvents();

  // Emulate PeerDisconnected signals from wpa_supplicant.
  EXPECT_CALL(cb, Run(LocalDevice::DeviceEvent::kPeerDisconnected, _))
      .Times(num_of_peers);
  for (int peer_id = 0; peer_id < num_of_peers; peer_id++) {
    auto peer_path = DefaultPeerObjectPath(peer_id);

    go_device_->PeerDisconnected(peer_path);

    EXPECT_FALSE(base::Contains(go_device_->group_peers_, peer_path));
    EXPECT_EQ(go_device_->group_peers_.size(), num_of_peers - peer_id - 1);
  }
  DispatchPendingEvents();
}

TEST_F(P2PDeviceTest, PeerJoinAndDisconnect_WhileNotReady) {
  int num_of_peers = 10;

  // Emulate PeerJoined signals from wpa_supplicant.
  EXPECT_CALL(cb, Run(LocalDevice::DeviceEvent::kPeerConnected, _)).Times(0);
  EXPECT_CALL(control_interface_, CreateSupplicantPeerProxy(_)).Times(0);
  for (int peer_id = 0; peer_id < num_of_peers; peer_id++) {
    go_device_->PeerJoined(DefaultPeerObjectPath(peer_id));
    EXPECT_EQ(go_device_->group_peers_.size(), 0);
  }
  DispatchPendingEvents();

  // Emulate PeerDisconnected signals from wpa_supplicant.
  EXPECT_CALL(cb, Run(LocalDevice::DeviceEvent::kPeerDisconnected, _)).Times(0);
  for (int peer_id = 0; peer_id < num_of_peers; peer_id++) {
    go_device_->PeerDisconnected(DefaultPeerObjectPath(peer_id));
    EXPECT_EQ(go_device_->group_peers_.size(), 0);
  }
  DispatchPendingEvents();
}

TEST_F(P2PDeviceTest, CreateAndRemove) {
  // Start device
  EXPECT_EQ(go_device_->state_, P2PDevice::P2PDeviceState::kUninitialized);
  EXPECT_TRUE(go_device_->Start());
  EXPECT_EQ(go_device_->service_, nullptr);
  EXPECT_EQ(go_device_->state_, P2PDevice::P2PDeviceState::kReady);

  // Initiate group creation with a new service.
  auto service0 = std::make_unique<MockP2PService>(
      go_device_, kP2PSSID, kP2PPassphrase, kP2PFrequency);
  EXPECT_CALL(*supplicant_primary_p2pdevice_proxy_, GroupAdd(_));
  EXPECT_TRUE(go_device_->CreateGroup(std::move(service0)));
  EXPECT_NE(go_device_->service_, nullptr);
  EXPECT_EQ(go_device_->service_->state(),
            LocalService::LocalServiceState::kStateStarting);
  EXPECT_EQ(go_device_->state_, P2PDevice::P2PDeviceState::kGOStarting);

  // Emulate GroupStarted signal from wpa_supplicant.
  EXPECT_CALL(cb, Run(LocalDevice::DeviceEvent::kLinkUp, _)).Times(1);
  EXPECT_CALL(cb, Run(LocalDevice::DeviceEvent::kNetworkUp, _)).Times(1);
  EXPECT_CALL(control_interface_,
              CreateSupplicantInterfaceProxy(_, kInterfacePath));
  EXPECT_CALL(control_interface_,
              CreateSupplicantP2PDeviceProxy(_, kInterfacePath));
  go_device_->GroupStarted(DefaultGroupStartedProperties());
  EXPECT_NE(go_device_->supplicant_interface_proxy_, nullptr);
  EXPECT_NE(go_device_->supplicant_p2pdevice_proxy_, nullptr);
  EXPECT_NE(go_device_->supplicant_group_proxy_, nullptr);
  EXPECT_EQ(go_device_->link_name_.value(), kInterfaceName);
  EXPECT_EQ(go_device_->group_ssid_, kP2PSSID);
  EXPECT_EQ(go_device_->group_bssid_, kP2PBSSID);
  EXPECT_EQ(go_device_->group_frequency_, kP2PFrequency);
  EXPECT_EQ(go_device_->group_passphrase_, kP2PPassphrase);
  EXPECT_EQ(go_device_->state_, P2PDevice::P2PDeviceState::kGOConfiguring);

  // Emulate OnGroupNetworkStarted callback from patchpanel.
  go_device_->OnGroupNetworkStarted(MakeFd(),
                                    {.network_id = kLocalOnlyNetworkId});

  // Attempting to create group again should be a no-op and and return false.
  auto service1 = std::make_unique<MockP2PService>(
      go_device_, kP2PSSID, kP2PPassphrase, kP2PFrequency);
  EXPECT_CALL(*supplicant_primary_p2pdevice_proxy_, GroupAdd(_)).Times(0);
  EXPECT_FALSE(go_device_->CreateGroup(std::move(service1)));
  EXPECT_EQ(go_device_->state_, P2PDevice::P2PDeviceState::kGOActive);

  // Remove group.
  EXPECT_CALL(*supplicant_p2pdevice_proxy_, Disconnect());
  EXPECT_TRUE(go_device_->RemoveGroup());
  EXPECT_EQ(go_device_->service_, nullptr);
  EXPECT_EQ(go_device_->state_, P2PDevice::P2PDeviceState::kGOStopping);

  // Emulate GroupFinished signal from wpa_supplicant
  EXPECT_CALL(cb, Run(LocalDevice::DeviceEvent::kLinkDown, _)).Times(1);
  go_device_->GroupFinished(DefaultGroupFinishedProperties());
  EXPECT_EQ(go_device_->supplicant_interface_proxy_, nullptr);
  EXPECT_EQ(go_device_->supplicant_p2pdevice_proxy_, nullptr);
  EXPECT_EQ(go_device_->supplicant_group_proxy_, nullptr);
  EXPECT_EQ(go_device_->state_, P2PDevice::P2PDeviceState::kReady);
  DispatchPendingEvents();

  // Stop device
  EXPECT_TRUE(go_device_->Stop());
  EXPECT_EQ(go_device_->state_, P2PDevice::P2PDeviceState::kUninitialized);
}

TEST_F(P2PDeviceTest, ConnectAndDisconnect) {
  // Start device
  EXPECT_EQ(client_device_->state_, P2PDevice::P2PDeviceState::kUninitialized);
  EXPECT_TRUE(client_device_->Start());
  EXPECT_EQ(client_device_->service_, nullptr);
  EXPECT_EQ(client_device_->state_, P2PDevice::P2PDeviceState::kReady);

  // Initiate connection with a new service.
  auto service0 = std::make_unique<MockP2PService>(
      client_device_, kP2PSSID, kP2PPassphrase, kP2PFrequency);
  EXPECT_CALL(*supplicant_primary_p2pdevice_proxy_, AddPersistentGroup(_, _));
  EXPECT_CALL(*supplicant_primary_p2pdevice_proxy_, GroupAdd(_));
  EXPECT_TRUE(client_device_->Connect(std::move(service0)));
  EXPECT_NE(client_device_->service_, nullptr);
  EXPECT_EQ(client_device_->service_->state(),
            LocalService::LocalServiceState::kStateStarting);
  EXPECT_EQ(client_device_->supplicant_persistent_group_path_, kGroupPath);
  EXPECT_EQ(client_device_->state_,
            P2PDevice::P2PDeviceState::kClientAssociating);

  // Emulate GroupStarted signal from wpa_supplicant.
  EXPECT_CALL(cb, Run(LocalDevice::DeviceEvent::kLinkUp, _)).Times(1);
  EXPECT_CALL(cb, Run(LocalDevice::DeviceEvent::kNetworkUp, _)).Times(1);
  EXPECT_CALL(control_interface_,
              CreateSupplicantInterfaceProxy(_, kInterfacePath));
  EXPECT_CALL(control_interface_,
              CreateSupplicantP2PDeviceProxy(_, kInterfacePath));
  client_device_->GroupStarted(DefaultGroupStartedProperties());
  EXPECT_NE(client_device_->supplicant_interface_proxy_, nullptr);
  EXPECT_NE(client_device_->supplicant_p2pdevice_proxy_, nullptr);
  EXPECT_NE(client_device_->supplicant_group_proxy_, nullptr);
  EXPECT_EQ(client_device_->link_name_.value(), kInterfaceName);
  EXPECT_EQ(client_device_->group_ssid_, kP2PSSID);
  EXPECT_EQ(client_device_->group_bssid_, kP2PBSSID);
  EXPECT_EQ(client_device_->group_frequency_, kP2PFrequency);
  EXPECT_EQ(client_device_->group_passphrase_, kP2PPassphrase);
  EXPECT_EQ(client_device_->state_,
            P2PDevice::P2PDeviceState::kClientConfiguring);
  // Emulate IP address received event.
  client_device_->OnConnectionUpdated(kInterfaceIdx);

  // Attempting to connect again should be a no-op and and return false.
  auto service1 = std::make_unique<MockP2PService>(
      client_device_, kP2PSSID, kP2PPassphrase, kP2PFrequency);
  EXPECT_CALL(*supplicant_primary_p2pdevice_proxy_, AddPersistentGroup(_, _))
      .Times(0);
  EXPECT_CALL(*supplicant_primary_p2pdevice_proxy_, GroupAdd(_)).Times(0);
  EXPECT_FALSE(client_device_->Connect(std::move(service1)));
  EXPECT_EQ(client_device_->state_,
            P2PDevice::P2PDeviceState::kClientConnected);

  // Disconnect.
  EXPECT_CALL(*supplicant_p2pdevice_proxy_, Disconnect());
  EXPECT_TRUE(client_device_->Disconnect());
  EXPECT_EQ(client_device_->service_, nullptr);
  EXPECT_EQ(client_device_->state_,
            P2PDevice::P2PDeviceState::kClientDisconnecting);

  // Emulate GroupFinished signal from wpa_supplicant
  EXPECT_CALL(cb, Run(LocalDevice::DeviceEvent::kLinkDown, _)).Times(1);
  EXPECT_CALL(*supplicant_primary_p2pdevice_proxy_, RemovePersistentGroup(_));
  client_device_->GroupFinished(DefaultGroupFinishedProperties());
  EXPECT_EQ(client_device_->supplicant_interface_proxy_, nullptr);
  EXPECT_EQ(client_device_->supplicant_p2pdevice_proxy_, nullptr);
  EXPECT_EQ(client_device_->supplicant_group_proxy_, nullptr);
  EXPECT_TRUE(
      client_device_->supplicant_persistent_group_path_.value().empty());
  EXPECT_EQ(client_device_->state_, P2PDevice::P2PDeviceState::kReady);
  DispatchPendingEvents();

  // Stop device
  EXPECT_TRUE(client_device_->Stop());
  EXPECT_EQ(client_device_->state_, P2PDevice::P2PDeviceState::kUninitialized);
}

TEST_F(P2PDeviceTest, BadState_Client) {
  // Initiate connection while device is uninitialized
  auto service = std::make_unique<MockP2PService>(
      client_device_, kP2PSSID, kP2PPassphrase, kP2PFrequency);
  EXPECT_CALL(*supplicant_primary_p2pdevice_proxy_, AddPersistentGroup(_, _))
      .Times(0);
  EXPECT_CALL(*supplicant_primary_p2pdevice_proxy_, GroupAdd(_)).Times(0);
  EXPECT_FALSE(client_device_->Connect(std::move(service)));
  EXPECT_EQ(client_device_->state_, P2PDevice::P2PDeviceState::kUninitialized);

  // Disconnect while not connected
  EXPECT_FALSE(client_device_->Disconnect());
  EXPECT_EQ(client_device_->state_, P2PDevice::P2PDeviceState::kUninitialized);

  // Start client_device_
  EXPECT_TRUE(client_device_->Start());
  EXPECT_EQ(client_device_->state_, P2PDevice::P2PDeviceState::kReady);

  // Initiate connection with a new service.
  service = std::make_unique<MockP2PService>(client_device_, kP2PSSID,
                                             kP2PPassphrase, kP2PFrequency);
  EXPECT_CALL(*supplicant_primary_p2pdevice_proxy_, AddPersistentGroup(_, _));
  EXPECT_CALL(*supplicant_primary_p2pdevice_proxy_, GroupAdd(_));
  EXPECT_TRUE(client_device_->Connect(std::move(service)));
  EXPECT_NE(client_device_->service_, nullptr);
  EXPECT_EQ(client_device_->service_->state(),
            LocalService::LocalServiceState::kStateStarting);
  EXPECT_EQ(client_device_->supplicant_persistent_group_path_, kGroupPath);
  EXPECT_EQ(client_device_->state_,
            P2PDevice::P2PDeviceState::kClientAssociating);

  // Emulate GroupStarted signal from wpa_supplicant.
  EXPECT_CALL(control_interface_,
              CreateSupplicantInterfaceProxy(_, kInterfacePath));
  EXPECT_CALL(control_interface_,
              CreateSupplicantP2PDeviceProxy(_, kInterfacePath));
  client_device_->GroupStarted(DefaultGroupStartedProperties());
  EXPECT_NE(client_device_->supplicant_interface_proxy_, nullptr);
  EXPECT_NE(client_device_->supplicant_p2pdevice_proxy_, nullptr);
  EXPECT_NE(client_device_->supplicant_group_proxy_, nullptr);
  EXPECT_EQ(client_device_->link_name_.value(), kInterfaceName);
  EXPECT_EQ(client_device_->group_ssid_, kP2PSSID);
  EXPECT_EQ(client_device_->group_bssid_, kP2PBSSID);
  EXPECT_EQ(client_device_->group_frequency_, kP2PFrequency);
  EXPECT_EQ(client_device_->group_passphrase_, kP2PPassphrase);
  EXPECT_EQ(client_device_->state_,
            P2PDevice::P2PDeviceState::kClientConfiguring);

  // Attempting to connect again should be a no-op and and return false.
  service = std::make_unique<MockP2PService>(client_device_, kP2PSSID,
                                             kP2PPassphrase, kP2PFrequency);
  EXPECT_CALL(*supplicant_primary_p2pdevice_proxy_, AddPersistentGroup(_, _))
      .Times(0);
  EXPECT_CALL(*supplicant_primary_p2pdevice_proxy_, GroupAdd(_)).Times(0);
  EXPECT_FALSE(client_device_->Connect(std::move(service)));
  EXPECT_EQ(client_device_->state_,
            P2PDevice::P2PDeviceState::kClientConfiguring);

  // Disconnect.
  EXPECT_CALL(*supplicant_p2pdevice_proxy_, Disconnect());
  EXPECT_TRUE(client_device_->Disconnect());
  EXPECT_EQ(client_device_->service_, nullptr);
  EXPECT_EQ(client_device_->state_,
            P2PDevice::P2PDeviceState::kClientDisconnecting);

  // Emulate GroupFinished signal from wpa_supplicant
  EXPECT_CALL(*supplicant_primary_p2pdevice_proxy_, RemovePersistentGroup(_));
  client_device_->GroupFinished(DefaultGroupFinishedProperties());
  EXPECT_EQ(client_device_->supplicant_interface_proxy_, nullptr);
  EXPECT_EQ(client_device_->supplicant_p2pdevice_proxy_, nullptr);
  EXPECT_EQ(client_device_->supplicant_group_proxy_, nullptr);
  EXPECT_TRUE(
      client_device_->supplicant_persistent_group_path_.value().empty());
  EXPECT_EQ(client_device_->state_, P2PDevice::P2PDeviceState::kReady);

  // Stop device
  EXPECT_TRUE(client_device_->Stop());
  EXPECT_EQ(client_device_->state_, P2PDevice::P2PDeviceState::kUninitialized);

  // Initiate connection while device is uninitialized
  service = std::make_unique<MockP2PService>(client_device_, kP2PSSID,
                                             kP2PPassphrase, kP2PFrequency);
  EXPECT_CALL(*supplicant_primary_p2pdevice_proxy_, AddPersistentGroup(_, _))
      .Times(0);
  EXPECT_CALL(*supplicant_primary_p2pdevice_proxy_, GroupAdd(_)).Times(0);
  EXPECT_FALSE(client_device_->Connect(std::move(service)));
  EXPECT_EQ(client_device_->state_, P2PDevice::P2PDeviceState::kUninitialized);

  // Disconnect while not connected
  EXPECT_FALSE(client_device_->Disconnect());
  EXPECT_EQ(client_device_->state_, P2PDevice::P2PDeviceState::kUninitialized);
}

TEST_F(P2PDeviceTest, BadState_GO) {
  // Initiate group creation while device is uninitialized
  auto service = std::make_unique<MockP2PService>(
      go_device_, kP2PSSID, kP2PPassphrase, kP2PFrequency);
  EXPECT_CALL(*supplicant_primary_p2pdevice_proxy_, GroupAdd(_)).Times(0);
  EXPECT_FALSE(go_device_->CreateGroup(std::move(service)));
  EXPECT_EQ(go_device_->state_, P2PDevice::P2PDeviceState::kUninitialized);

  // Remove group while not created
  EXPECT_FALSE(go_device_->RemoveGroup());
  EXPECT_EQ(go_device_->state_, P2PDevice::P2PDeviceState::kUninitialized);

  // Start device
  EXPECT_TRUE(go_device_->Start());
  EXPECT_EQ(go_device_->state_, P2PDevice::P2PDeviceState::kReady);

  // Initiate group creation with a new service.
  service = std::make_unique<MockP2PService>(go_device_, kP2PSSID,
                                             kP2PPassphrase, kP2PFrequency);
  EXPECT_CALL(*supplicant_primary_p2pdevice_proxy_, GroupAdd(_));
  EXPECT_TRUE(go_device_->CreateGroup(std::move(service)));
  EXPECT_NE(go_device_->service_, nullptr);
  EXPECT_EQ(go_device_->service_->state(),
            LocalService::LocalServiceState::kStateStarting);
  EXPECT_EQ(go_device_->state_, P2PDevice::P2PDeviceState::kGOStarting);

  // Emulate GroupStarted signal from wpa_supplicant.
  EXPECT_CALL(control_interface_,
              CreateSupplicantInterfaceProxy(_, kInterfacePath));
  EXPECT_CALL(control_interface_,
              CreateSupplicantP2PDeviceProxy(_, kInterfacePath));
  go_device_->GroupStarted(DefaultGroupStartedProperties());
  EXPECT_NE(go_device_->supplicant_interface_proxy_, nullptr);
  EXPECT_NE(go_device_->supplicant_p2pdevice_proxy_, nullptr);
  EXPECT_NE(go_device_->supplicant_group_proxy_, nullptr);
  EXPECT_EQ(go_device_->link_name_.value(), kInterfaceName);
  EXPECT_EQ(go_device_->group_ssid_, kP2PSSID);
  EXPECT_EQ(go_device_->group_bssid_, kP2PBSSID);
  EXPECT_EQ(go_device_->group_frequency_, kP2PFrequency);
  EXPECT_EQ(go_device_->group_passphrase_, kP2PPassphrase);
  EXPECT_EQ(go_device_->state_, P2PDevice::P2PDeviceState::kGOConfiguring);

  // Attempting to create group again should be a no-op and and return false.
  service = std::make_unique<MockP2PService>(go_device_, kP2PSSID,
                                             kP2PPassphrase, kP2PFrequency);
  EXPECT_CALL(*supplicant_primary_p2pdevice_proxy_, GroupAdd(_)).Times(0);
  EXPECT_FALSE(go_device_->CreateGroup(std::move(service)));
  EXPECT_EQ(go_device_->state_, P2PDevice::P2PDeviceState::kGOConfiguring);

  // Remove group.
  EXPECT_CALL(*supplicant_p2pdevice_proxy_, Disconnect());
  EXPECT_TRUE(go_device_->RemoveGroup());
  EXPECT_EQ(go_device_->service_, nullptr);
  EXPECT_EQ(go_device_->state_, P2PDevice::P2PDeviceState::kGOStopping);

  // Emulate GroupFinished signal from wpa_supplicant
  go_device_->GroupFinished(DefaultGroupFinishedProperties());
  EXPECT_EQ(go_device_->supplicant_interface_proxy_, nullptr);
  EXPECT_EQ(go_device_->supplicant_p2pdevice_proxy_, nullptr);
  EXPECT_EQ(go_device_->supplicant_group_proxy_, nullptr);
  EXPECT_EQ(go_device_->state_, P2PDevice::P2PDeviceState::kReady);

  // Stop device
  EXPECT_TRUE(go_device_->Stop());
  EXPECT_EQ(go_device_->state_, P2PDevice::P2PDeviceState::kUninitialized);

  // Initiate group creation while device is uninitialized
  service = std::make_unique<MockP2PService>(go_device_, kP2PSSID,
                                             kP2PPassphrase, kP2PFrequency);
  EXPECT_CALL(*supplicant_primary_p2pdevice_proxy_, GroupAdd(_)).Times(0);
  EXPECT_FALSE(go_device_->CreateGroup(std::move(service)));
  EXPECT_EQ(go_device_->state_, P2PDevice::P2PDeviceState::kUninitialized);

  // Remove group while not created
  EXPECT_FALSE(go_device_->RemoveGroup());
  EXPECT_EQ(go_device_->state_, P2PDevice::P2PDeviceState::kUninitialized);
}

TEST_F(P2PDeviceTest, ConnectToSupplicantInterfaceProxy) {
  EXPECT_CALL(control_interface_,
              CreateSupplicantInterfaceProxy(_, kInterfacePath));
  EXPECT_TRUE(go_device_->ConnectToSupplicantInterfaceProxy(kInterfacePath));
  EXPECT_NE(go_device_->supplicant_interface_proxy_, nullptr);
}

TEST_F(P2PDeviceTest, ConnectToSupplicantInterfaceProxy_WhileConnected) {
  EXPECT_CALL(control_interface_,
              CreateSupplicantInterfaceProxy(_, kInterfacePath));
  EXPECT_TRUE(go_device_->ConnectToSupplicantInterfaceProxy(kInterfacePath));
  EXPECT_NE(go_device_->supplicant_interface_proxy_, nullptr);

  EXPECT_CALL(control_interface_, CreateSupplicantInterfaceProxy(_, _))
      .Times(0);
  EXPECT_FALSE(go_device_->ConnectToSupplicantInterfaceProxy(kInterfacePath));
  EXPECT_NE(go_device_->supplicant_interface_proxy_, nullptr);
}

TEST_F(P2PDeviceTest, ConnectToSupplicantInterfaceProxy_Failure) {
  EXPECT_CALL(control_interface_,
              CreateSupplicantInterfaceProxy(_, kInterfacePath))
      .WillOnce(Return(nullptr));
  EXPECT_FALSE(go_device_->ConnectToSupplicantInterfaceProxy(kInterfacePath));
  EXPECT_EQ(go_device_->supplicant_interface_proxy_, nullptr);
}

TEST_F(P2PDeviceTest, ConnectToSupplicantP2PDeviceProxy) {
  EXPECT_CALL(control_interface_,
              CreateSupplicantP2PDeviceProxy(_, kInterfacePath));
  EXPECT_TRUE(go_device_->ConnectToSupplicantP2PDeviceProxy(kInterfacePath));
  EXPECT_NE(go_device_->supplicant_p2pdevice_proxy_, nullptr);
}

TEST_F(P2PDeviceTest, ConnectToSupplicantP2PDeviceProxy_WhileConnected) {
  EXPECT_CALL(control_interface_,
              CreateSupplicantP2PDeviceProxy(_, kInterfacePath));
  EXPECT_TRUE(go_device_->ConnectToSupplicantP2PDeviceProxy(kInterfacePath));
  EXPECT_NE(go_device_->supplicant_p2pdevice_proxy_, nullptr);

  EXPECT_CALL(control_interface_, CreateSupplicantP2PDeviceProxy(_, _))
      .Times(0);
  EXPECT_FALSE(go_device_->ConnectToSupplicantP2PDeviceProxy(kInterfacePath));
  EXPECT_NE(go_device_->supplicant_p2pdevice_proxy_, nullptr);
}

TEST_F(P2PDeviceTest, ConnectToSupplicantP2PDeviceProxy_Failure) {
  EXPECT_CALL(control_interface_,
              CreateSupplicantP2PDeviceProxy(_, kInterfacePath))
      .WillOnce(Return(nullptr));
  EXPECT_FALSE(go_device_->ConnectToSupplicantP2PDeviceProxy(kInterfacePath));
  EXPECT_EQ(go_device_->supplicant_p2pdevice_proxy_, nullptr);
}

TEST_F(P2PDeviceTest, ConnectToSupplicantGroupProxy) {
  EXPECT_CALL(control_interface_, CreateSupplicantGroupProxy(_, kGroupPath));
  EXPECT_TRUE(go_device_->ConnectToSupplicantGroupProxy(kGroupPath));
  EXPECT_NE(go_device_->supplicant_group_proxy_, nullptr);
}

TEST_F(P2PDeviceTest, ConnectToSupplicantGroupProxy_WhileConnected) {
  EXPECT_CALL(control_interface_, CreateSupplicantGroupProxy(_, kGroupPath));
  EXPECT_TRUE(go_device_->ConnectToSupplicantGroupProxy(kGroupPath));
  EXPECT_NE(go_device_->supplicant_group_proxy_, nullptr);

  EXPECT_CALL(control_interface_, CreateSupplicantGroupProxy(_, _)).Times(0);
  EXPECT_FALSE(go_device_->ConnectToSupplicantGroupProxy(kGroupPath));
  EXPECT_NE(go_device_->supplicant_group_proxy_, nullptr);
}

TEST_F(P2PDeviceTest, ConnectToSupplicantGroupProxy_Failure) {
  EXPECT_CALL(control_interface_, CreateSupplicantGroupProxy(_, kGroupPath))
      .WillOnce(Return(nullptr));
  EXPECT_FALSE(go_device_->ConnectToSupplicantGroupProxy(kGroupPath));
  EXPECT_EQ(go_device_->supplicant_group_proxy_, nullptr);
}

TEST_F(P2PDeviceTest, SetupGroup) {
  KeyValueStore properties = DefaultGroupStartedProperties();
  EXPECT_CALL(control_interface_,
              CreateSupplicantInterfaceProxy(_, kInterfacePath));
  EXPECT_CALL(control_interface_,
              CreateSupplicantP2PDeviceProxy(_, kInterfacePath));
  EXPECT_CALL(control_interface_, CreateSupplicantGroupProxy(_, kGroupPath));
  go_device_->SetupGroup(properties);
  EXPECT_NE(go_device_->supplicant_interface_proxy_, nullptr);
  EXPECT_NE(go_device_->supplicant_p2pdevice_proxy_, nullptr);
  EXPECT_NE(go_device_->supplicant_group_proxy_, nullptr);
  EXPECT_EQ(go_device_->link_name_.value(), kInterfaceName);
  EXPECT_EQ(go_device_->group_ssid_, kP2PSSID);
  EXPECT_EQ(go_device_->group_bssid_, kP2PBSSID);
  EXPECT_EQ(go_device_->group_frequency_, kP2PFrequency);
  EXPECT_EQ(go_device_->group_passphrase_, kP2PPassphrase);
}

TEST_F(P2PDeviceTest, SetupGroup_EmptyProperties) {
  KeyValueStore properties;
  EXPECT_CALL(control_interface_, CreateSupplicantInterfaceProxy(_, _))
      .Times(0);
  EXPECT_CALL(control_interface_, CreateSupplicantP2PDeviceProxy(_, _))
      .Times(0);
  EXPECT_CALL(control_interface_, CreateSupplicantGroupProxy(_, _)).Times(0);
  go_device_->SetupGroup(properties);
  EXPECT_EQ(go_device_->supplicant_interface_proxy_, nullptr);
  EXPECT_EQ(go_device_->supplicant_p2pdevice_proxy_, nullptr);
  EXPECT_EQ(go_device_->supplicant_group_proxy_, nullptr);
}

TEST_F(P2PDeviceTest, SetupGroup_MissingGroupPath) {
  KeyValueStore properties;
  properties.Set<RpcIdentifier>(
      WPASupplicant::kGroupStartedPropertyInterfaceObject, kInterfacePath);
  EXPECT_CALL(control_interface_, CreateSupplicantInterfaceProxy(_, _))
      .Times(0);
  EXPECT_CALL(control_interface_, CreateSupplicantP2PDeviceProxy(_, _))
      .Times(0);
  EXPECT_CALL(control_interface_, CreateSupplicantGroupProxy(_, _)).Times(0);
  go_device_->SetupGroup(properties);
  EXPECT_EQ(go_device_->supplicant_interface_proxy_, nullptr);
  EXPECT_EQ(go_device_->supplicant_p2pdevice_proxy_, nullptr);
  EXPECT_EQ(go_device_->supplicant_group_proxy_, nullptr);
}

TEST_F(P2PDeviceTest, GroupStarted_WhileNotExpected) {
  // Start device
  EXPECT_EQ(go_device_->state_, P2PDevice::P2PDeviceState::kUninitialized);
  EXPECT_TRUE(go_device_->Start());
  CHECK_EQ(go_device_->state_, P2PDevice::P2PDeviceState::kReady);

  // Ignore unextected signal from wpa_supplicant.
  EXPECT_CALL(cb, Run(_, _)).Times(0);
  go_device_->GroupStarted(DefaultGroupStartedProperties());
  CHECK_EQ(go_device_->state_, P2PDevice::P2PDeviceState::kReady);
  DispatchPendingEvents();

  // Stop device
  go_device_->Stop();
  CHECK_EQ(go_device_->state_, P2PDevice::P2PDeviceState::kUninitialized);
}

TEST_F(P2PDeviceTest, GroupFinished_WhileGOStarting) {
  // Start device
  EXPECT_EQ(go_device_->state_, P2PDevice::P2PDeviceState::kUninitialized);
  EXPECT_TRUE(go_device_->Start());
  EXPECT_EQ(go_device_->service_, nullptr);
  EXPECT_EQ(go_device_->state_, P2PDevice::P2PDeviceState::kReady);

  // Initiate group creation with a new service.
  auto service = std::make_unique<MockP2PService>(
      go_device_, kP2PSSID, kP2PPassphrase, kP2PFrequency);
  EXPECT_CALL(*supplicant_primary_p2pdevice_proxy_, GroupAdd(_));
  EXPECT_TRUE(go_device_->CreateGroup(std::move(service)));
  EXPECT_NE(go_device_->service_, nullptr);
  EXPECT_EQ(go_device_->service_->state(),
            LocalService::LocalServiceState::kStateStarting);
  EXPECT_EQ(go_device_->state_, P2PDevice::P2PDeviceState::kGOStarting);

  // Emulate GroupFinished signal from wpa_supplicant (unknown failure).
  // Uexpected signal, we are going to ignore finished signal for group
  // which was never started.
  EXPECT_CALL(cb, Run(_, _)).Times(0);
  go_device_->GroupFinished(DefaultGroupFinishedProperties());
  EXPECT_EQ(go_device_->supplicant_interface_proxy_, nullptr);
  EXPECT_EQ(go_device_->supplicant_p2pdevice_proxy_, nullptr);
  EXPECT_EQ(go_device_->supplicant_group_proxy_, nullptr);
  EXPECT_EQ(go_device_->state_, P2PDevice::P2PDeviceState::kGOStarting);
  DispatchPendingEvents();

  // Stop device
  go_device_->Stop();
  CHECK_EQ(go_device_->state_, P2PDevice::P2PDeviceState::kUninitialized);
}

TEST_F(P2PDeviceTest, GroupFinished_WhileGOConfiguring) {
  // Start device
  EXPECT_EQ(go_device_->state_, P2PDevice::P2PDeviceState::kUninitialized);
  EXPECT_TRUE(go_device_->Start());
  EXPECT_EQ(go_device_->service_, nullptr);
  EXPECT_EQ(go_device_->state_, P2PDevice::P2PDeviceState::kReady);

  // Initiate group creation with a new service.
  auto service = std::make_unique<MockP2PService>(
      go_device_, kP2PSSID, kP2PPassphrase, kP2PFrequency);
  EXPECT_CALL(*supplicant_primary_p2pdevice_proxy_, GroupAdd(_));
  EXPECT_TRUE(go_device_->CreateGroup(std::move(service)));
  EXPECT_NE(go_device_->service_, nullptr);
  EXPECT_EQ(go_device_->service_->state(),
            LocalService::LocalServiceState::kStateStarting);
  EXPECT_EQ(go_device_->state_, P2PDevice::P2PDeviceState::kGOStarting);

  // Emulate GroupStarted signal from wpa_supplicant.
  EXPECT_CALL(cb, Run(LocalDevice::DeviceEvent::kLinkUp, _)).Times(1);
  EXPECT_CALL(control_interface_,
              CreateSupplicantInterfaceProxy(_, kInterfacePath));
  EXPECT_CALL(control_interface_,
              CreateSupplicantP2PDeviceProxy(_, kInterfacePath));
  go_device_->GroupStarted(DefaultGroupStartedProperties());
  EXPECT_NE(go_device_->supplicant_interface_proxy_, nullptr);
  EXPECT_NE(go_device_->supplicant_p2pdevice_proxy_, nullptr);
  EXPECT_NE(go_device_->supplicant_group_proxy_, nullptr);
  EXPECT_EQ(go_device_->link_name_.value(), kInterfaceName);
  EXPECT_EQ(go_device_->group_ssid_, kP2PSSID);
  EXPECT_EQ(go_device_->group_bssid_, kP2PBSSID);
  EXPECT_EQ(go_device_->group_frequency_, kP2PFrequency);
  EXPECT_EQ(go_device_->group_passphrase_, kP2PPassphrase);
  EXPECT_EQ(go_device_->state_, P2PDevice::P2PDeviceState::kGOConfiguring);

  // Emulate GroupFinished signal from wpa_supplicant (link failure).
  EXPECT_CALL(cb, Run(LocalDevice::DeviceEvent::kLinkFailure, _)).Times(1);
  go_device_->GroupFinished(DefaultGroupFinishedProperties());
  EXPECT_EQ(go_device_->supplicant_interface_proxy_, nullptr);
  EXPECT_EQ(go_device_->supplicant_p2pdevice_proxy_, nullptr);
  EXPECT_EQ(go_device_->supplicant_group_proxy_, nullptr);
  EXPECT_EQ(go_device_->state_, P2PDevice::P2PDeviceState::kGOStopping);
  DispatchPendingEvents();

  // Stop device
  go_device_->Stop();
  CHECK_EQ(go_device_->state_, P2PDevice::P2PDeviceState::kUninitialized);
}

TEST_F(P2PDeviceTest, GroupFinished_WhileGOActive) {
  // Start device
  EXPECT_EQ(go_device_->state_, P2PDevice::P2PDeviceState::kUninitialized);
  EXPECT_TRUE(go_device_->Start());
  EXPECT_EQ(go_device_->service_, nullptr);
  EXPECT_EQ(go_device_->state_, P2PDevice::P2PDeviceState::kReady);

  // Initiate group creation with a new service.
  auto service = std::make_unique<MockP2PService>(
      go_device_, kP2PSSID, kP2PPassphrase, kP2PFrequency);
  EXPECT_CALL(*supplicant_primary_p2pdevice_proxy_, GroupAdd(_));
  EXPECT_TRUE(go_device_->CreateGroup(std::move(service)));
  EXPECT_NE(go_device_->service_, nullptr);
  EXPECT_EQ(go_device_->service_->state(),
            LocalService::LocalServiceState::kStateStarting);
  EXPECT_EQ(go_device_->state_, P2PDevice::P2PDeviceState::kGOStarting);

  // Emulate GroupStarted signal from wpa_supplicant.
  EXPECT_CALL(cb, Run(LocalDevice::DeviceEvent::kLinkUp, _)).Times(1);
  EXPECT_CALL(cb, Run(LocalDevice::DeviceEvent::kNetworkUp, _)).Times(1);
  EXPECT_CALL(control_interface_,
              CreateSupplicantInterfaceProxy(_, kInterfacePath));
  EXPECT_CALL(control_interface_,
              CreateSupplicantP2PDeviceProxy(_, kInterfacePath));
  go_device_->GroupStarted(DefaultGroupStartedProperties());
  EXPECT_NE(go_device_->supplicant_interface_proxy_, nullptr);
  EXPECT_NE(go_device_->supplicant_p2pdevice_proxy_, nullptr);
  EXPECT_NE(go_device_->supplicant_group_proxy_, nullptr);
  EXPECT_EQ(go_device_->link_name_.value(), kInterfaceName);
  EXPECT_EQ(go_device_->group_ssid_, kP2PSSID);
  EXPECT_EQ(go_device_->group_bssid_, kP2PBSSID);
  EXPECT_EQ(go_device_->group_frequency_, kP2PFrequency);
  EXPECT_EQ(go_device_->group_passphrase_, kP2PPassphrase);
  EXPECT_EQ(go_device_->state_, P2PDevice::P2PDeviceState::kGOConfiguring);
  // Emulate OnGroupNetworkStarted callback from patchpanel.
  go_device_->OnGroupNetworkStarted(MakeFd(),
                                    {.network_id = kLocalOnlyNetworkId});

  // Emulate GroupFinished signal from wpa_supplicant (link failure).
  EXPECT_CALL(cb, Run(LocalDevice::DeviceEvent::kLinkFailure, _)).Times(1);
  go_device_->GroupFinished(DefaultGroupFinishedProperties());
  EXPECT_EQ(go_device_->supplicant_interface_proxy_, nullptr);
  EXPECT_EQ(go_device_->supplicant_p2pdevice_proxy_, nullptr);
  EXPECT_EQ(go_device_->supplicant_group_proxy_, nullptr);
  EXPECT_EQ(go_device_->state_, P2PDevice::P2PDeviceState::kGOStopping);
  DispatchPendingEvents();

  // Stop device
  go_device_->Stop();
  CHECK_EQ(go_device_->state_, P2PDevice::P2PDeviceState::kUninitialized);
}

TEST_F(P2PDeviceTest, GroupFinished_WhileClientAssociating) {
  // Start device
  EXPECT_EQ(client_device_->state_, P2PDevice::P2PDeviceState::kUninitialized);
  EXPECT_TRUE(client_device_->Start());
  EXPECT_EQ(client_device_->service_, nullptr);
  EXPECT_EQ(client_device_->state_, P2PDevice::P2PDeviceState::kReady);

  // Initiate group creation with a new service.
  auto service = std::make_unique<MockP2PService>(
      client_device_, kP2PSSID, kP2PPassphrase, kP2PFrequency);
  EXPECT_CALL(*supplicant_primary_p2pdevice_proxy_, GroupAdd(_));
  EXPECT_TRUE(client_device_->Connect(std::move(service)));
  EXPECT_NE(client_device_->service_, nullptr);
  EXPECT_EQ(client_device_->service_->state(),
            LocalService::LocalServiceState::kStateStarting);
  EXPECT_EQ(client_device_->state_,
            P2PDevice::P2PDeviceState::kClientAssociating);

  // Emulate GroupFinished signal from wpa_supplicant (unknown failure).
  // Uexpected signal, we are going to ignore finished signal for group
  // which was never started.
  EXPECT_CALL(cb, Run(_, _)).Times(0);
  client_device_->GroupFinished(DefaultGroupFinishedProperties());
  EXPECT_EQ(client_device_->supplicant_interface_proxy_, nullptr);
  EXPECT_EQ(client_device_->supplicant_p2pdevice_proxy_, nullptr);
  EXPECT_EQ(client_device_->supplicant_group_proxy_, nullptr);
  EXPECT_EQ(client_device_->state_,
            P2PDevice::P2PDeviceState::kClientAssociating);
  DispatchPendingEvents();

  // Stop device
  client_device_->Stop();
  CHECK_EQ(client_device_->state_, P2PDevice::P2PDeviceState::kUninitialized);
}

TEST_F(P2PDeviceTest, GroupFinished_WhileClientConfiguring) {
  // Start device
  EXPECT_EQ(client_device_->state_, P2PDevice::P2PDeviceState::kUninitialized);
  EXPECT_TRUE(client_device_->Start());
  EXPECT_EQ(client_device_->service_, nullptr);
  EXPECT_EQ(client_device_->state_, P2PDevice::P2PDeviceState::kReady);

  // Initiate group creation with a new service.
  auto service = std::make_unique<MockP2PService>(
      client_device_, kP2PSSID, kP2PPassphrase, kP2PFrequency);
  EXPECT_CALL(*supplicant_primary_p2pdevice_proxy_, GroupAdd(_));
  EXPECT_TRUE(client_device_->Connect(std::move(service)));
  EXPECT_NE(client_device_->service_, nullptr);
  EXPECT_EQ(client_device_->service_->state(),
            LocalService::LocalServiceState::kStateStarting);
  EXPECT_EQ(client_device_->state_,
            P2PDevice::P2PDeviceState::kClientAssociating);

  // Emulate GroupStarted signal from wpa_supplicant.
  EXPECT_CALL(cb, Run(LocalDevice::DeviceEvent::kLinkUp, _)).Times(1);
  EXPECT_CALL(control_interface_,
              CreateSupplicantInterfaceProxy(_, kInterfacePath));
  EXPECT_CALL(control_interface_,
              CreateSupplicantP2PDeviceProxy(_, kInterfacePath));
  client_device_->GroupStarted(DefaultGroupStartedProperties());
  EXPECT_NE(client_device_->supplicant_interface_proxy_, nullptr);
  EXPECT_NE(client_device_->supplicant_p2pdevice_proxy_, nullptr);
  EXPECT_NE(client_device_->supplicant_group_proxy_, nullptr);
  EXPECT_EQ(client_device_->link_name_.value(), kInterfaceName);
  EXPECT_EQ(client_device_->group_ssid_, kP2PSSID);
  EXPECT_EQ(client_device_->group_bssid_, kP2PBSSID);
  EXPECT_EQ(client_device_->group_frequency_, kP2PFrequency);
  EXPECT_EQ(client_device_->group_passphrase_, kP2PPassphrase);
  EXPECT_EQ(client_device_->state_,
            P2PDevice::P2PDeviceState::kClientConfiguring);

  // Emulate GroupFinished signal from wpa_supplicant (link failure).
  EXPECT_CALL(cb, Run(LocalDevice::DeviceEvent::kLinkFailure, _)).Times(1);
  client_device_->GroupFinished(DefaultGroupFinishedProperties());
  EXPECT_EQ(client_device_->supplicant_interface_proxy_, nullptr);
  EXPECT_EQ(client_device_->supplicant_p2pdevice_proxy_, nullptr);
  EXPECT_EQ(client_device_->supplicant_group_proxy_, nullptr);
  EXPECT_EQ(client_device_->state_,
            P2PDevice::P2PDeviceState::kClientDisconnecting);
  DispatchPendingEvents();

  // Stop device
  client_device_->Stop();
  CHECK_EQ(client_device_->state_, P2PDevice::P2PDeviceState::kUninitialized);
}

TEST_F(P2PDeviceTest, GroupFinished_WhileClientConnected) {
  // Start device
  EXPECT_EQ(client_device_->state_, P2PDevice::P2PDeviceState::kUninitialized);
  EXPECT_TRUE(client_device_->Start());
  EXPECT_EQ(client_device_->service_, nullptr);
  EXPECT_EQ(client_device_->state_, P2PDevice::P2PDeviceState::kReady);

  // Initiate group creation with a new service.
  auto service = std::make_unique<MockP2PService>(
      client_device_, kP2PSSID, kP2PPassphrase, kP2PFrequency);
  EXPECT_CALL(*supplicant_primary_p2pdevice_proxy_, GroupAdd(_));
  EXPECT_TRUE(client_device_->Connect(std::move(service)));
  EXPECT_NE(client_device_->service_, nullptr);
  EXPECT_EQ(client_device_->service_->state(),
            LocalService::LocalServiceState::kStateStarting);
  EXPECT_EQ(client_device_->state_,
            P2PDevice::P2PDeviceState::kClientAssociating);

  // Emulate GroupStarted signal from wpa_supplicant.
  EXPECT_CALL(cb, Run(LocalDevice::DeviceEvent::kLinkUp, _)).Times(1);
  EXPECT_CALL(cb, Run(LocalDevice::DeviceEvent::kNetworkUp, _)).Times(1);
  EXPECT_CALL(control_interface_,
              CreateSupplicantInterfaceProxy(_, kInterfacePath));
  EXPECT_CALL(control_interface_,
              CreateSupplicantP2PDeviceProxy(_, kInterfacePath));
  client_device_->GroupStarted(DefaultGroupStartedProperties());
  EXPECT_NE(client_device_->supplicant_interface_proxy_, nullptr);
  EXPECT_NE(client_device_->supplicant_p2pdevice_proxy_, nullptr);
  EXPECT_NE(client_device_->supplicant_group_proxy_, nullptr);
  EXPECT_EQ(client_device_->link_name_.value(), kInterfaceName);
  EXPECT_EQ(client_device_->group_ssid_, kP2PSSID);
  EXPECT_EQ(client_device_->group_bssid_, kP2PBSSID);
  EXPECT_EQ(client_device_->group_frequency_, kP2PFrequency);
  EXPECT_EQ(client_device_->group_passphrase_, kP2PPassphrase);
  EXPECT_EQ(client_device_->state_,
            P2PDevice::P2PDeviceState::kClientConfiguring);
  // Emulate IP address received event.
  client_device_->OnConnectionUpdated(kInterfaceIdx);
  EXPECT_EQ(client_device_->state_,
            P2PDevice::P2PDeviceState::kClientConnected);

  DispatchPendingEvents();

  // Emulate GroupFinished signal from wpa_supplicant (link failure).
  EXPECT_CALL(cb, Run(LocalDevice::DeviceEvent::kLinkFailure, _)).Times(1);
  client_device_->GroupFinished(DefaultGroupFinishedProperties());
  EXPECT_EQ(client_device_->supplicant_interface_proxy_, nullptr);
  EXPECT_EQ(client_device_->supplicant_p2pdevice_proxy_, nullptr);
  EXPECT_EQ(client_device_->supplicant_group_proxy_, nullptr);
  EXPECT_EQ(client_device_->state_,
            P2PDevice::P2PDeviceState::kClientDisconnecting);
  DispatchPendingEvents();

  // Stop device
  client_device_->Stop();
  CHECK_EQ(client_device_->state_, P2PDevice::P2PDeviceState::kUninitialized);
}

TEST_F(P2PDeviceTest, GroupFinished_WhileNotExpected) {
  // Start device
  EXPECT_EQ(go_device_->state_, P2PDevice::P2PDeviceState::kUninitialized);
  EXPECT_TRUE(go_device_->Start());
  CHECK_EQ(go_device_->state_, P2PDevice::P2PDeviceState::kReady);

  // Ignore unextected signal from wpa_supplicant.
  EXPECT_CALL(cb, Run(_, _)).Times(0);
  go_device_->GroupFinished(DefaultGroupFinishedProperties());
  CHECK_EQ(go_device_->state_, P2PDevice::P2PDeviceState::kReady);
  DispatchPendingEvents();

  // Stop device
  go_device_->Stop();
  CHECK_EQ(go_device_->state_, P2PDevice::P2PDeviceState::kUninitialized);
}

TEST_F(P2PDeviceTest, GO_StartGroupNetworkImmediateFail) {
  // Start device
  EXPECT_TRUE(go_device_->Start());
  auto service = std::make_unique<MockP2PService>(
      go_device_, kP2PSSID, kP2PPassphrase, kP2PFrequency);
  EXPECT_TRUE(go_device_->CreateGroup(std::move(service)));
  EXPECT_CALL(*patchpanel_, CreateLocalOnlyNetwork(kInterfaceName, _))
      .WillOnce(Return(false));
  go_device_->GroupStarted(DefaultGroupStartedProperties());
  EXPECT_CALL(cb, Run(LocalDevice::DeviceEvent::kLinkUp, _)).Times(1);
  EXPECT_CALL(cb, Run(LocalDevice::DeviceEvent::kNetworkFailure, _)).Times(1);
  DispatchPendingEvents();

  // Stop device
  go_device_->Stop();
  CHECK_EQ(go_device_->state_, P2PDevice::P2PDeviceState::kUninitialized);
}

TEST_F(P2PDeviceTest, GO_StartGroupNetworkFail) {
  // Start device
  EXPECT_TRUE(go_device_->Start());
  auto service = std::make_unique<MockP2PService>(
      go_device_, kP2PSSID, kP2PPassphrase, kP2PFrequency);
  EXPECT_TRUE(go_device_->CreateGroup(std::move(service)));
  go_device_->GroupStarted(DefaultGroupStartedProperties());
  EXPECT_CALL(cb, Run(LocalDevice::DeviceEvent::kLinkUp, _)).Times(1);
  DispatchPendingEvents();
  EXPECT_EQ(go_device_->state_, P2PDevice::P2PDeviceState::kGOConfiguring);

  // Emulate OnGroupNetworkStarted callback from patchpanel with invalid FD.
  go_device_->OnGroupNetworkStarted(base::ScopedFD(-1), {});
  EXPECT_CALL(cb, Run(LocalDevice::DeviceEvent::kNetworkFailure, _)).Times(1);
  DispatchPendingEvents();

  // Stop device
  go_device_->Stop();
  CHECK_EQ(go_device_->state_, P2PDevice::P2PDeviceState::kUninitialized);
}

TEST_F(P2PDeviceTest, Client_AcquireClientIPFail) {
  // Start device
  EXPECT_TRUE(client_device_->Start());
  auto service = std::make_unique<MockP2PService>(
      client_device_, kP2PSSID, kP2PPassphrase, kP2PFrequency);
  EXPECT_TRUE(client_device_->Connect(std::move(service)));
  client_device_->GroupStarted(DefaultGroupStartedProperties());
  EXPECT_EQ(client_device_->state_,
            P2PDevice::P2PDeviceState::kClientConfiguring);
  EXPECT_CALL(cb, Run(LocalDevice::DeviceEvent::kLinkUp, _)).Times(1);
  DispatchPendingEvents();

  // Emulate OnNetworkStopped event.
  client_device_->OnNetworkStopped(kInterfaceIdx, true);
  EXPECT_CALL(cb, Run(LocalDevice::DeviceEvent::kNetworkFailure, _)).Times(1);
  DispatchPendingEvents();

  // Stop device
  EXPECT_TRUE(client_device_->Stop());
  EXPECT_EQ(client_device_->state_, P2PDevice::P2PDeviceState::kUninitialized);
}

TEST_F(P2PDeviceTest, Client_NetworkStopped) {
  // Start device
  EXPECT_TRUE(client_device_->Start());
  auto service = std::make_unique<MockP2PService>(
      client_device_, kP2PSSID, kP2PPassphrase, kP2PFrequency);
  EXPECT_TRUE(client_device_->Connect(std::move(service)));
  client_device_->GroupStarted(DefaultGroupStartedProperties());
  EXPECT_CALL(cb, Run(LocalDevice::DeviceEvent::kLinkUp, _)).Times(1);
  DispatchPendingEvents();
  // Emulate IP address received event.
  client_device_->OnConnectionUpdated(kInterfaceIdx);
  EXPECT_EQ(client_device_->state_,
            P2PDevice::P2PDeviceState::kClientConnected);
  EXPECT_CALL(cb, Run(LocalDevice::DeviceEvent::kNetworkUp, _)).Times(1);
  DispatchPendingEvents();

  // Emulate OnNetworkStopped event.
  client_device_->OnNetworkStopped(kInterfaceIdx, false);
  EXPECT_CALL(cb, Run(LocalDevice::DeviceEvent::kNetworkDown, _)).Times(1);
  DispatchPendingEvents();

  // Stop device
  EXPECT_TRUE(client_device_->Stop());
  EXPECT_EQ(client_device_->state_, P2PDevice::P2PDeviceState::kUninitialized);
}

}  // namespace shill
