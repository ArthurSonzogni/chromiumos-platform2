// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/wifi/p2p_manager.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

#include <base/files/scoped_temp_dir.h>
#include <base/test/mock_callback.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "shill/mock_control.h"
#include "shill/mock_manager.h"
#include "shill/mock_metrics.h"
#include "shill/supplicant/mock_supplicant_p2pdevice_proxy.h"
#include "shill/supplicant/mock_supplicant_process_proxy.h"
#include "shill/supplicant/wpa_supplicant.h"
#include "shill/test_event_dispatcher.h"
#include "shill/wifi/mock_p2p_device.h"
#include "shill/wifi/mock_wifi_phy.h"
#include "shill/wifi/mock_wifi_provider.h"

using testing::_;
using testing::ByMove;
using testing::DoAll;
using testing::InvokeArgument;
using testing::NiceMock;
using testing::Return;
using testing::SaveArg;
using testing::SetArgPointee;
using testing::StrictMock;
using testing::Test;
using testing::Unused;

namespace shill {

namespace {
const int32_t kDefaultShillId = 0;
const char kPrimaryInterfaceName[] = "wlan0";
const char kP2PDeviceInterfaceName[] = "p2p-wlan0-0";
constexpr uint32_t kPhyIndex = 5678;
const RpcIdentifier kPrimaryIfacePath = RpcIdentifier("/interface/wlan0");
}  // namespace

class P2PManagerTest : public testing::Test {
 public:
  P2PManagerTest()
      : temp_dir_(MakeTempDir()),
        path_(temp_dir_.GetPath().value()),
        manager_(
            &control_interface_, &dispatcher_, &metrics_, path_, path_, path_),
        wifi_provider_(new NiceMock<MockWiFiProvider>(&manager_)),
        p2p_manager_(wifi_provider_->p2p_manager()),
        supplicant_process_proxy_(new NiceMock<MockSupplicantProcessProxy>()),
        supplicant_primary_p2pdevice_proxy_(
            new NiceMock<MockSupplicantP2PDeviceProxy>()) {
    // Replace the Manager's WiFi provider with a mock.
    manager_.wifi_provider_.reset(wifi_provider_);
    // Update the Manager's map from technology to provider.
    manager_.UpdateProviderMapping();
    manager_.supplicant_manager()->set_proxy(supplicant_process_proxy_);
    ON_CALL(*wifi_provider_, GetPrimaryLinkName())
        .WillByDefault(Return(kPrimaryInterfaceName));
    ON_CALL(*supplicant_process_proxy_, CreateInterface(_, _))
        .WillByDefault(
            DoAll(SetArgPointee<1>(kPrimaryIfacePath), Return(true)));
    ON_CALL(control_interface_,
            CreateSupplicantP2PDeviceProxy(_, kPrimaryIfacePath))
        .WillByDefault(
            Return(ByMove(std::move(supplicant_primary_p2pdevice_proxy_))));
  }

  ~P2PManagerTest() override = default;

  void SetAllowed(P2PManager* p2p_manager, bool allowed) {
    Error error;
    PropertyStore store;
    p2p_manager->InitPropertyStore(&store);
    store.SetBoolProperty(kP2PAllowedProperty, allowed, &error);
    EXPECT_TRUE(error.IsSuccess());
  }

  KeyValueStore GetCapabilities(P2PManager* p2p_manager) {
    Error error;
    KeyValueStore caps = p2p_manager->GetCapabilities(&error);
    EXPECT_TRUE(error.IsSuccess());
    return caps;
  }

  KeyValueStores GetGroupInfos(P2PManager* p2p_manager) {
    Error error;
    KeyValueStores groupInfos = p2p_manager->GetGroupInfos(&error);
    EXPECT_TRUE(error.IsSuccess());
    EXPECT_EQ(groupInfos.size(), p2p_manager_->p2p_group_owners_.size());
    return groupInfos;
  }

  KeyValueStores GetClientInfos(P2PManager* p2p_manager) {
    Error error;
    KeyValueStores clientInfos = p2p_manager->GetClientInfos(&error);
    EXPECT_TRUE(error.IsSuccess());
    EXPECT_EQ(clientInfos.size(), p2p_manager_->p2p_clients_.size());
    return clientInfos;
  }

  base::ScopedTempDir MakeTempDir() {
    base::ScopedTempDir temp_dir;
    EXPECT_TRUE(temp_dir.CreateUniqueTempDir());
    return temp_dir;
  }

  void DispatchPendingEvents() { dispatcher_.DispatchPendingEvents(); }

  void FastForward(bool is_start) {
    auto time = is_start ? p2p_manager_->kP2PStartTimeout
                         : p2p_manager_->kP2PStopTimeout;
    dispatcher_.task_environment().FastForwardBy(time);
  }

  KeyValueStore CreateP2PGroupWithProperties(MockP2PDevice* p2p_device,
                                             KeyValueStore properties) {
    KeyValueStore response_dict;
    base::MockOnceCallback<void(KeyValueStore)> cb;
    SetDefaultDeviceLinkName(p2p_device);

    EXPECT_CALL(*wifi_provider_, CreateP2PDevice)
        .WillOnce(
            [p2p_device](Unused, Unused, Unused, Unused,
                         base::OnceCallback<void(P2PDeviceRefPtr)> success_cb,
                         Unused) {
              std::move(success_cb).Run(p2p_device);
              return true;
            });
    EXPECT_CALL(*wifi_provider_, RequestLocalDeviceCreation(_, _, _))
        .WillOnce([](Unused, Unused, base::OnceClosure create_device_cb) {
          std::move(create_device_cb).Run();
          return true;
        });
    EXPECT_CALL(*p2p_device, CreateGroup(_)).WillOnce(Return(true));
    EXPECT_TRUE(IsActionTimerCancelled());
    p2p_manager_->CreateP2PGroup(cb.Get(), properties);
    EXPECT_FALSE(IsActionTimerCancelled());

    EXPECT_CALL(cb, Run(_)).WillOnce(SaveArg<0>(&response_dict));
    OnP2PDeviceEvent(LocalDevice::DeviceEvent::kNetworkUp, p2p_device);
    DispatchPendingEvents();
    EXPECT_TRUE(IsActionTimerCancelled());
    return response_dict;
  }

  KeyValueStore CreateP2PGroup(MockP2PDevice* p2p_device) {
    KeyValueStore properties;
    properties.Set<std::string>(kP2PDeviceSSID, "DIRECT-ab");
    properties.Set<std::string>(kP2PDevicePassphrase, "test0000");
    properties.Set<int32_t>(kP2PDeviceFrequency, 1234);
    properties.Set<int32_t>(kP2PDevicePriority, 1);
    return CreateP2PGroupWithProperties(p2p_device, properties);
  }

  KeyValueStore ConnectToP2PGroupWithProperties(MockP2PDevice* p2p_device,
                                                KeyValueStore properties) {
    KeyValueStore response_dict;
    base::MockOnceCallback<void(KeyValueStore)> cb;
    SetDefaultDeviceLinkName(p2p_device);

    EXPECT_CALL(*wifi_provider_, CreateP2PDevice)
        .WillOnce(
            [p2p_device](Unused, Unused, Unused, Unused,
                         base::OnceCallback<void(P2PDeviceRefPtr)> success_cb,
                         Unused) {
              std::move(success_cb).Run(p2p_device);
              return true;
            });
    EXPECT_CALL(*wifi_provider_, RequestLocalDeviceCreation(_, _, _))
        .WillOnce([](Unused, Unused, base::OnceClosure create_device_cb) {
          std::move(create_device_cb).Run();
          return true;
        });
    EXPECT_CALL(*p2p_device, Connect(_)).WillOnce(Return(true));
    EXPECT_TRUE(IsActionTimerCancelled());
    p2p_manager_->ConnectToP2PGroup(cb.Get(), properties);
    EXPECT_FALSE(IsActionTimerCancelled());
    EXPECT_CALL(cb, Run(_)).WillOnce(SaveArg<0>(&response_dict));
    OnP2PDeviceEvent(LocalDevice::DeviceEvent::kNetworkUp, p2p_device);
    DispatchPendingEvents();
    EXPECT_TRUE(IsActionTimerCancelled());
    return response_dict;
  }

  KeyValueStore ConnectToP2PGroup(MockP2PDevice* p2p_device) {
    KeyValueStore properties;
    properties.Set<std::string>(kP2PDeviceSSID, "DIRECT-ab");
    properties.Set<std::string>(kP2PDevicePassphrase, "test0000");
    properties.Set<int32_t>(kP2PDeviceFrequency, 1234);
    properties.Set<int32_t>(kP2PDevicePriority, 1);
    return ConnectToP2PGroupWithProperties(p2p_device, properties);
  }

  std::string DefaultInterfaceName(int32_t shill_id) {
    return "p2p-wlan0-" + std::to_string(shill_id);
  }

  RpcIdentifier DefaultInterfacePath(int32_t shill_id) {
    return RpcIdentifier("/interface/" + DefaultInterfaceName(shill_id));
  }

  RpcIdentifier DefaultGroupPath(int32_t shill_id) {
    return RpcIdentifier("/interface/" + DefaultInterfaceName(shill_id) +
                         "/Group/xx");
  }

  KeyValueStore DefaultGroupStartedProperties(int32_t shill_id) {
    KeyValueStore properties;
    properties.Set<RpcIdentifier>(
        WPASupplicant::kGroupStartedPropertyInterfaceObject,
        DefaultInterfacePath(shill_id));
    properties.Set<RpcIdentifier>(
        WPASupplicant::kGroupStartedPropertyGroupObject,
        DefaultGroupPath(shill_id));
    return properties;
  }

  KeyValueStore DefaultGroupFinishedProperties(int32_t shill_id) {
    KeyValueStore properties;
    properties.Set<RpcIdentifier>(
        WPASupplicant::kGroupFinishedPropertyInterfaceObject,
        DefaultInterfacePath(shill_id));
    properties.Set<RpcIdentifier>(
        WPASupplicant::kGroupFinishedPropertyGroupObject,
        DefaultGroupPath(shill_id));
    return properties;
  }

  void PostGroupStarted(int32_t shill_id) {
    PostGroupStarted(DefaultGroupStartedProperties(shill_id));
  }

  void PostGroupStarted(const KeyValueStore& properties) {
    p2p_manager_->GroupStarted(properties);
  }

  void PostGroupFinished(int32_t shill_id) {
    PostGroupFinished(DefaultGroupFinishedProperties(shill_id));
  }

  void PostGroupFinished(const KeyValueStore& properties) {
    p2p_manager_->GroupFinished(properties);
  }

  void PostGroupFormationFailure(const std::string& reason = "Unknown") {
    p2p_manager_->GroupFormationFailure(reason);
  }

  void SetDefaultDeviceLinkName(P2PDevice* p2p_device) {
    p2p_device->link_name_ = kP2PDeviceInterfaceName;
  }

  bool IsActionTimerCancelled() {
    return p2p_manager_->action_timer_callback_.IsCancelled();
  }

  void OnDeviceCreationFailed(LocalDevice::IfaceType iface_type) {
    p2p_manager_->OnDeviceCreationFailed(iface_type);
  }

  void OnP2PDeviceEvent(LocalDevice::DeviceEvent event, P2PDevice* p2p_device) {
    p2p_manager_->OnP2PDeviceEvent(event, p2p_device);
  }

 protected:
  StrictMock<base::MockRepeatingCallback<void(LocalDevice::DeviceEvent,
                                              const LocalDevice*)>>
      event_cb_;
  NiceMock<MockControl> control_interface_;
  EventDispatcherForTest dispatcher_;
  NiceMock<MockMetrics> metrics_;
  base::ScopedTempDir temp_dir_;
  std::string path_;
  MockManager manager_;
  MockWiFiProvider* wifi_provider_;
  P2PManager* p2p_manager_;
  // Map of unique IDs to P2P group owners.
  std::map<uint32_t, MockP2PDevice> p2p_group_owners_;
  // Map of unique IDs to P2P clients.
  std::map<uint32_t, MockP2PDevice> p2p_clients_;
  MockSupplicantProcessProxy* supplicant_process_proxy_;
  std::unique_ptr<MockSupplicantP2PDeviceProxy>
      supplicant_primary_p2pdevice_proxy_;
};

TEST_F(P2PManagerTest, SetP2PAllowed) {
  SetAllowed(p2p_manager_, true);
  EXPECT_EQ(p2p_manager_->allowed_, true);
  SetAllowed(p2p_manager_, false);
  EXPECT_EQ(p2p_manager_->allowed_, false);
}

TEST_F(P2PManagerTest, GetP2PCapabilities) {
  std::unique_ptr<NiceMock<MockWiFiPhy>> phy(
      new NiceMock<MockWiFiPhy>(kPhyIndex));
  const std::vector<const WiFiPhy*> phys = {phy.get()};
  ON_CALL(*wifi_provider_, GetPhys()).WillByDefault(Return(phys));

  // P2P not supported
  ON_CALL(*phy, SupportP2PMode()).WillByDefault(Return(false));
  KeyValueStore caps = GetCapabilities(p2p_manager_);
  EXPECT_TRUE(caps.Contains<Boolean>(kP2PCapabilitiesP2PSupportedProperty));
  EXPECT_FALSE(caps.Get<Boolean>(kP2PCapabilitiesP2PSupportedProperty));
  EXPECT_FALSE(caps.Contains<String>(kP2PCapabilitiesGroupReadinessProperty));
  EXPECT_FALSE(caps.Contains<String>(kP2PCapabilitiesClientReadinessProperty));
  EXPECT_FALSE(
      caps.Contains<Integers>(kP2PCapabilitiesSupportedChannelsProperty));
  EXPECT_FALSE(
      caps.Contains<Integers>(kP2PCapabilitiesPreferredChannelsProperty));

  // P2P supported but only with SCC mode
  ON_CALL(*phy, SupportP2PMode()).WillByDefault(Return(true));
  ON_CALL(*phy, SupportsConcurrency(_)).WillByDefault(Return(1));
  caps = GetCapabilities(p2p_manager_);
  EXPECT_TRUE(caps.Contains<Boolean>(kP2PCapabilitiesP2PSupportedProperty));
  EXPECT_FALSE(caps.Get<Boolean>(kP2PCapabilitiesP2PSupportedProperty));
  EXPECT_FALSE(caps.Contains<String>(kP2PCapabilitiesGroupReadinessProperty));
  EXPECT_FALSE(caps.Contains<String>(kP2PCapabilitiesClientReadinessProperty));
  EXPECT_FALSE(
      caps.Contains<Integers>(kP2PCapabilitiesSupportedChannelsProperty));
  EXPECT_FALSE(
      caps.Contains<Integers>(kP2PCapabilitiesPreferredChannelsProperty));

  // P2P supported and MCC supported
  ON_CALL(*phy, SupportP2PMode()).WillByDefault(Return(true));
  ON_CALL(*phy, SupportsConcurrency(_)).WillByDefault(Return(2));
  caps = GetCapabilities(p2p_manager_);
  EXPECT_TRUE(caps.Contains<Boolean>(kP2PCapabilitiesP2PSupportedProperty));
  EXPECT_TRUE(caps.Get<Boolean>(kP2PCapabilitiesP2PSupportedProperty));
  // TODO(b/295050788, b/299295629): it requires P2P/STA concurrency level
  // and interface combination checking to be supported by wifi phy.
  EXPECT_TRUE(caps.Contains<String>(kP2PCapabilitiesGroupReadinessProperty));
  EXPECT_EQ(caps.Get<String>(kP2PCapabilitiesGroupReadinessProperty),
            kP2PCapabilitiesGroupReadinessNotReady);
  EXPECT_TRUE(caps.Contains<String>(kP2PCapabilitiesClientReadinessProperty));
  EXPECT_EQ(caps.Get<String>(kP2PCapabilitiesClientReadinessProperty),
            kP2PCapabilitiesClientReadinessNotReady);
  EXPECT_TRUE(
      caps.Contains<Integers>(kP2PCapabilitiesSupportedChannelsProperty));
  EXPECT_TRUE(
      caps.Get<Integers>(kP2PCapabilitiesSupportedChannelsProperty).empty());
  EXPECT_TRUE(
      caps.Contains<Integers>(kP2PCapabilitiesPreferredChannelsProperty));
  EXPECT_TRUE(
      caps.Get<Integers>(kP2PCapabilitiesPreferredChannelsProperty).empty());
}

TEST_F(P2PManagerTest, GetP2PGroupInfos) {
  KeyValueStore pattern;
  MockP2PDevice* p2p_device = new NiceMock<MockP2PDevice>(
      &manager_, LocalDevice::IfaceType::kP2PGO, "wlan0", 0, 0,
      WiFiPhy::Priority(0), event_cb_.Get());
  for (int i = 0; i < 10; i++) {
    pattern.Set<uint32_t>(kP2PGroupInfoShillIDProperty, i);
    pattern.Set<String>(kP2PGroupInfoStateProperty, kP2PGroupInfoStateIdle);

    p2p_manager_->p2p_group_owners_[i] = p2p_device;
    EXPECT_CALL(*p2p_device, GetGroupInfo())
        .Times(i + 1)
        .WillRepeatedly(Return(pattern));

    KeyValueStores groupInfos = GetGroupInfos(p2p_manager_);
    EXPECT_EQ(groupInfos.size(), i + 1);
    for (auto& result : groupInfos) {
      EXPECT_EQ(result.Get<uint32_t>(kP2PGroupInfoShillIDProperty), i);
      EXPECT_EQ(result.Get<String>(kP2PGroupInfoStateProperty),
                kP2PGroupInfoStateIdle);
    }
  }
}

TEST_F(P2PManagerTest, GetP2PClientInfos) {
  KeyValueStore pattern;
  MockP2PDevice* p2p_device = new NiceMock<MockP2PDevice>(
      &manager_, LocalDevice::IfaceType::kP2PClient, "wlan0", 0, 0,
      WiFiPhy::Priority(0), event_cb_.Get());
  for (int i = 0; i < 10; i++) {
    pattern.Set<uint32_t>(kP2PClientInfoShillIDProperty, i);
    pattern.Set<String>(kP2PClientInfoStateProperty, kP2PClientInfoStateIdle);

    p2p_manager_->p2p_clients_[i] = p2p_device;
    EXPECT_CALL(*p2p_device, GetClientInfo())
        .Times(i + 1)
        .WillRepeatedly(Return(pattern));

    KeyValueStores clientInfos = GetClientInfos(p2p_manager_);
    EXPECT_EQ(clientInfos.size(), i + 1);
    for (auto& result : clientInfos) {
      EXPECT_EQ(result.Get<uint32_t>(kP2PClientInfoShillIDProperty), i);
      EXPECT_EQ(result.Get<String>(kP2PClientInfoStateProperty),
                kP2PClientInfoStateIdle);
    }
  }
}

TEST_F(P2PManagerTest, ConnectAndDisconnectClient) {
  MockP2PDevice* p2p_device = new NiceMock<MockP2PDevice>(
      &manager_, LocalDevice::IfaceType::kP2PClient, "wlan0", 0, 0,
      WiFiPhy::Priority(0), event_cb_.Get());
  base::MockOnceCallback<void(KeyValueStore)> cb;

  int32_t expected_shill_id = p2p_manager_->next_unique_id_;

  KeyValueStore info_pattern;
  info_pattern.Set<int32_t>(kP2PClientInfoShillIDProperty, expected_shill_id);
  info_pattern.Set<String>(kP2PClientInfoStateProperty,
                           kP2PClientInfoStateConnected);
  KeyValueStores info_result;

  info_result = GetClientInfos(p2p_manager_);
  ASSERT_EQ(info_result.size(), 0);
  KeyValueStore response_dict = ConnectToP2PGroup(p2p_device);
  ASSERT_EQ(response_dict.Get<std::string>(kP2PResultCode),
            kConnectToP2PGroupResultSuccess);
  ASSERT_EQ(response_dict.Get<int32_t>(kP2PDeviceShillID), expected_shill_id);
  ASSERT_EQ(p2p_manager_->p2p_clients_[expected_shill_id], p2p_device);

  EXPECT_CALL(*p2p_device, GetClientInfo()).WillOnce(Return(info_pattern));
  info_result = GetClientInfos(p2p_manager_);
  ASSERT_EQ(info_result.size(), 1);
  EXPECT_EQ(info_result[0].Get<int32_t>(kP2PClientInfoShillIDProperty),
            expected_shill_id);
  EXPECT_EQ(info_result[0].Get<String>(kP2PClientInfoStateProperty),
            kP2PClientInfoStateConnected);
  EXPECT_CALL(*p2p_device, Disconnect()).WillOnce(Return(true));
  p2p_manager_->DisconnectFromP2PGroup(cb.Get(), expected_shill_id);
  EXPECT_FALSE(IsActionTimerCancelled());

  EXPECT_CALL(*p2p_device, state())
      .WillRepeatedly(Return(P2PDevice::P2PDeviceState::kReady));
  EXPECT_CALL(cb, Run(_)).WillOnce(SaveArg<0>(&response_dict));
  OnP2PDeviceEvent(LocalDevice::DeviceEvent::kLinkDown, p2p_device);
  DispatchPendingEvents();
  EXPECT_TRUE(IsActionTimerCancelled());
  ASSERT_EQ(response_dict.Get<std::string>(kP2PResultCode),
            kDisconnectFromP2PGroupResultSuccess);
  ASSERT_EQ(p2p_manager_->p2p_clients_.count(expected_shill_id), 0);

  info_result = GetClientInfos(p2p_manager_);
  ASSERT_EQ(info_result.size(), 0);
}

TEST_F(P2PManagerTest, CreateAndDestroyGroup) {
  MockP2PDevice* p2p_device = new NiceMock<MockP2PDevice>(
      &manager_, LocalDevice::IfaceType::kP2PGO, "wlan0", 0, 0,
      WiFiPhy::Priority(0), event_cb_.Get());
  base::MockOnceCallback<void(KeyValueStore)> cb;
  int32_t expected_shill_id = p2p_manager_->next_unique_id_;

  KeyValueStore info_pattern;
  info_pattern.Set<int32_t>(kP2PGroupInfoShillIDProperty, expected_shill_id);
  info_pattern.Set<String>(kP2PGroupInfoStateProperty,
                           kP2PGroupInfoStateActive);
  KeyValueStores info_result;

  info_result = GetGroupInfos(p2p_manager_);
  ASSERT_EQ(info_result.size(), 0);

  KeyValueStore response_dict = CreateP2PGroup(p2p_device);
  ASSERT_EQ(response_dict.Get<std::string>(kP2PResultCode),
            kCreateP2PGroupResultSuccess);
  ASSERT_EQ(response_dict.Get<int32_t>(kP2PDeviceShillID), expected_shill_id);
  ASSERT_EQ(p2p_manager_->p2p_group_owners_[expected_shill_id], p2p_device);

  EXPECT_CALL(*p2p_device, GetGroupInfo()).WillOnce(Return(info_pattern));
  info_result = GetGroupInfos(p2p_manager_);
  ASSERT_EQ(info_result.size(), 1);
  EXPECT_EQ(info_result[0].Get<int32_t>(kP2PGroupInfoShillIDProperty),
            expected_shill_id);
  EXPECT_EQ(info_result[0].Get<String>(kP2PGroupInfoStateProperty),
            kP2PGroupInfoStateActive);

  EXPECT_CALL(*p2p_device, RemoveGroup()).WillOnce(Return(true));
  p2p_manager_->DestroyP2PGroup(cb.Get(), expected_shill_id);
  EXPECT_FALSE(IsActionTimerCancelled());

  EXPECT_CALL(*p2p_device, state())
      .WillRepeatedly(Return(P2PDevice::P2PDeviceState::kReady));
  EXPECT_CALL(cb, Run(_)).WillOnce(SaveArg<0>(&response_dict));
  OnP2PDeviceEvent(LocalDevice::DeviceEvent::kLinkDown, p2p_device);
  DispatchPendingEvents();
  EXPECT_TRUE(IsActionTimerCancelled());
  ASSERT_EQ(response_dict.Get<std::string>(kP2PResultCode),
            kDestroyP2PGroupResultSuccess);
  ASSERT_EQ(p2p_manager_->p2p_group_owners_.count(expected_shill_id), 0);

  info_result = GetGroupInfos(p2p_manager_);
  ASSERT_EQ(info_result.size(), 0);
}

TEST_F(P2PManagerTest, DisconnectWithoutConnect) {
  base::MockOnceCallback<void(KeyValueStore)> cb;
  KeyValueStore response_dict;
  int32_t shill_id = 0;

  EXPECT_CALL(cb, Run(_)).WillOnce(SaveArg<0>(&response_dict));
  p2p_manager_->DisconnectFromP2PGroup(cb.Get(), shill_id);
  DispatchPendingEvents();
  ASSERT_EQ(response_dict.Get<std::string>(kP2PResultCode),
            kDisconnectFromP2PGroupResultNotConnected);
}

TEST_F(P2PManagerTest, DestroyWithoutCreate) {
  base::MockOnceCallback<void(KeyValueStore)> cb;
  KeyValueStore response_dict;
  int32_t shill_id = 0;

  EXPECT_CALL(cb, Run(_)).WillOnce(SaveArg<0>(&response_dict));
  p2p_manager_->DestroyP2PGroup(cb.Get(), shill_id);
  DispatchPendingEvents();
  ASSERT_EQ(response_dict.Get<std::string>(kP2PResultCode),
            kDestroyP2PGroupResultNoGroup);
}

TEST_F(P2PManagerTest, ShillIDs) {
  KeyValueStore properties;
  properties.Set<std::string>(kP2PDevicePassphrase, "test0000");
  properties.Set<int32_t>(kP2PDeviceFrequency, 1234);
  properties.Set<int32_t>(kP2PDevicePriority, 1);
  int32_t current_id = p2p_manager_->next_unique_id_;
  std::string ssid;
  base::MockOnceCallback<void(KeyValueStore)> cb;

  for (int i = 0; i < 10; i++) {
    MockP2PDevice* p2p_device = new NiceMock<MockP2PDevice>(
        &manager_, LocalDevice::IfaceType::kP2PClient, "wlan0", 0, current_id,
        WiFiPhy::Priority(0), event_cb_.Get());
    // Each client needs a unique SSID.
    std::string ssid = "DIRECT-ab-" + std::to_string(i);
    properties.Set<std::string>(kP2PDeviceSSID, ssid);
    ConnectToP2PGroupWithProperties(p2p_device, properties);
    EXPECT_EQ(p2p_manager_->p2p_clients_[current_id], p2p_device);
    PostGroupStarted(current_id);
    current_id++;
  }

  for (int i = 0; i < 10; i++) {
    MockP2PDevice* p2p_device = new NiceMock<MockP2PDevice>(
        &manager_, LocalDevice::IfaceType::kP2PGO, "wlan0", 0, current_id,
        WiFiPhy::Priority(0), event_cb_.Get());
    // Each group owner needs a unique SSID.
    std::string ssid = "DIRECT-ab-" + std::to_string(i);
    properties.Set<std::string>(kP2PDeviceSSID, ssid);
    CreateP2PGroupWithProperties(p2p_device, properties);
    EXPECT_EQ(p2p_manager_->p2p_group_owners_[current_id], p2p_device);
    PostGroupStarted(current_id);
    current_id++;
  }
}

TEST_F(P2PManagerTest, MissingArgs_CreateGroup) {
  KeyValueStore properties;
  properties.Set<int32_t>(kP2PDevicePriority, 1);
  MockP2PDevice* p2p_device = new NiceMock<MockP2PDevice>(
      &manager_, LocalDevice::IfaceType::kP2PGO, "wlan0", 0, 0,
      WiFiPhy::Priority(0), event_cb_.Get());
  base::MockOnceCallback<void(KeyValueStore)> cb;
  int32_t expected_shill_id = p2p_manager_->next_unique_id_;

  KeyValueStore response_dict =
      CreateP2PGroupWithProperties(p2p_device, properties);
  ASSERT_EQ(response_dict.Get<std::string>(kP2PResultCode),
            kCreateP2PGroupResultSuccess);
  ASSERT_EQ(p2p_manager_->p2p_group_owners_[expected_shill_id], p2p_device);
}

TEST_F(P2PManagerTest, MissingArgs_CreateGroup_PriorityMissing) {
  KeyValueStore properties;
  base::MockOnceCallback<void(KeyValueStore)> cb;
  KeyValueStore response_dict;
  int32_t expected_shill_id = p2p_manager_->next_unique_id_;

  EXPECT_CALL(*wifi_provider_, CreateP2PDevice).Times(0);
  EXPECT_CALL(cb, Run(_)).WillOnce(SaveArg<0>(&response_dict));
  p2p_manager_->CreateP2PGroup(cb.Get(), properties);
  DispatchPendingEvents();
  EXPECT_TRUE(IsActionTimerCancelled());
  ASSERT_EQ(response_dict.Get<std::string>(kP2PResultCode),
            kCreateP2PGroupResultInvalidArguments);
  ASSERT_EQ(p2p_manager_->p2p_group_owners_.count(expected_shill_id), 0);
}

TEST_F(P2PManagerTest, MissingArgs_ConnectClient) {
  KeyValueStore properties;
  base::MockOnceCallback<void(KeyValueStore)> cb;
  KeyValueStore response_dict;
  int32_t expected_shill_id = p2p_manager_->next_unique_id_;

  EXPECT_CALL(*wifi_provider_, CreateP2PDevice).Times(0);
  EXPECT_CALL(cb, Run(_)).WillOnce(SaveArg<0>(&response_dict));
  p2p_manager_->ConnectToP2PGroup(cb.Get(), properties);
  DispatchPendingEvents();
  EXPECT_TRUE(IsActionTimerCancelled());
  ASSERT_EQ(response_dict.Get<std::string>(kP2PResultCode),
            kConnectToP2PGroupResultInvalidArguments);
  ASSERT_EQ(p2p_manager_->p2p_clients_.count(expected_shill_id), 0);
}

TEST_F(P2PManagerTest, BadPriority) {
  KeyValueStore properties;
  properties.Set<int32_t>(kP2PDevicePriority,
                          WiFiPhy::Priority::kMaximumPriority + 1);
  base::MockOnceCallback<void(KeyValueStore)> cb;
  KeyValueStore response_dict;
  int32_t expected_shill_id = p2p_manager_->next_unique_id_;

  EXPECT_CALL(*wifi_provider_, CreateP2PDevice).Times(0);
  EXPECT_CALL(cb, Run(_)).WillOnce(SaveArg<0>(&response_dict));
  p2p_manager_->CreateP2PGroup(cb.Get(), properties);
  DispatchPendingEvents();
  EXPECT_TRUE(IsActionTimerCancelled());
  ASSERT_EQ(response_dict.Get<std::string>(kP2PResultCode),
            kCreateP2PGroupResultInvalidArguments);
  ASSERT_EQ(p2p_manager_->p2p_group_owners_.count(expected_shill_id), 0);
}

TEST_F(P2PManagerTest, GroupStarted) {
  KeyValueStore properties = DefaultGroupStartedProperties(kDefaultShillId);
  RpcIdentifier interface_path = properties.Get<RpcIdentifier>(
      WPASupplicant::kGroupStartedPropertyInterfaceObject);
  MockP2PDevice* p2p_device = new NiceMock<MockP2PDevice>(
      &manager_, LocalDevice::IfaceType::kP2PGO, "wlan0", 0, kDefaultShillId,
      WiFiPhy::Priority(0), event_cb_.Get());

  CreateP2PGroup(p2p_device);

  EXPECT_EQ(p2p_manager_->p2p_group_owners_[kDefaultShillId], p2p_device);
  EXPECT_EQ(p2p_manager_->supplicant_primary_p2pdevice_pending_event_delegate_,
            p2p_device);

  EXPECT_CALL(*p2p_device, GroupStarted(properties)).Times(1);
  PostGroupStarted(properties);

  EXPECT_EQ(p2p_manager_->supplicant_primary_p2pdevice_pending_event_delegate_,
            nullptr);
  EXPECT_EQ(p2p_manager_
                ->supplicant_primary_p2pdevice_event_delegates_[interface_path],
            p2p_device);
}

TEST_F(P2PManagerTest, GroupStarted_IgnoreDuplicates) {
  KeyValueStore properties = DefaultGroupStartedProperties(kDefaultShillId);
  RpcIdentifier interface_path = properties.Get<RpcIdentifier>(
      WPASupplicant::kGroupStartedPropertyInterfaceObject);
  MockP2PDevice* p2p_device = new NiceMock<MockP2PDevice>(
      &manager_, LocalDevice::IfaceType::kP2PGO, "wlan0", 0, kDefaultShillId,
      WiFiPhy::Priority(0), event_cb_.Get());

  CreateP2PGroup(p2p_device);

  EXPECT_EQ(p2p_manager_->p2p_group_owners_[kDefaultShillId], p2p_device);
  EXPECT_EQ(p2p_manager_->supplicant_primary_p2pdevice_pending_event_delegate_,
            p2p_device);

  EXPECT_CALL(*p2p_device, GroupStarted(properties)).Times(1);
  PostGroupStarted(properties);

  EXPECT_EQ(p2p_manager_->supplicant_primary_p2pdevice_pending_event_delegate_,
            nullptr);
  EXPECT_EQ(p2p_manager_
                ->supplicant_primary_p2pdevice_event_delegates_[interface_path],
            p2p_device);

  EXPECT_CALL(*p2p_device, GroupStarted(_)).Times(0);
  for (int i = 0; i < 10; i++)
    PostGroupStarted(properties);

  EXPECT_EQ(p2p_manager_->supplicant_primary_p2pdevice_pending_event_delegate_,
            nullptr);
  EXPECT_EQ(p2p_manager_
                ->supplicant_primary_p2pdevice_event_delegates_[interface_path],
            p2p_device);
}

TEST_F(P2PManagerTest, GroupStarted_IgnoreMissingDevice) {
  KeyValueStore properties = DefaultGroupStartedProperties(kDefaultShillId);
  RpcIdentifier interface_path = properties.Get<RpcIdentifier>(
      WPASupplicant::kGroupStartedPropertyInterfaceObject);
  MockP2PDevice* p2p_device = new NiceMock<MockP2PDevice>(
      &manager_, LocalDevice::IfaceType::kP2PGO, "wlan0", 0, kDefaultShillId,
      WiFiPhy::Priority(0), event_cb_.Get());

  CreateP2PGroup(p2p_device);

  EXPECT_EQ(p2p_manager_->p2p_group_owners_[kDefaultShillId], p2p_device);
  EXPECT_EQ(p2p_manager_->supplicant_primary_p2pdevice_pending_event_delegate_,
            p2p_device);

  EXPECT_CALL(*p2p_device, GroupStarted(properties)).Times(1);
  PostGroupStarted(properties);

  EXPECT_EQ(p2p_manager_->supplicant_primary_p2pdevice_pending_event_delegate_,
            nullptr);
  EXPECT_EQ(p2p_manager_
                ->supplicant_primary_p2pdevice_event_delegates_[interface_path],
            p2p_device);

  EXPECT_CALL(*p2p_device, GroupStarted(_)).Times(0);
  for (int i = 0; i < 10; i++)
    PostGroupStarted(kDefaultShillId + 1);

  EXPECT_EQ(p2p_manager_->supplicant_primary_p2pdevice_pending_event_delegate_,
            nullptr);
  EXPECT_EQ(p2p_manager_
                ->supplicant_primary_p2pdevice_event_delegates_[interface_path],
            p2p_device);

  interface_path = DefaultInterfacePath(kDefaultShillId + 1);
  EXPECT_EQ(p2p_manager_
                ->supplicant_primary_p2pdevice_event_delegates_[interface_path],
            nullptr);
}

TEST_F(P2PManagerTest, GroupStarted_IgnoreMissingProperties) {
  KeyValueStore properties; /* empty properties */
  MockP2PDevice* p2p_device = new NiceMock<MockP2PDevice>(
      &manager_, LocalDevice::IfaceType::kP2PGO, "wlan0", 0, kDefaultShillId,
      WiFiPhy::Priority(0), event_cb_.Get());

  CreateP2PGroup(p2p_device);

  EXPECT_EQ(p2p_manager_->p2p_group_owners_[kDefaultShillId], p2p_device);
  EXPECT_EQ(p2p_manager_->supplicant_primary_p2pdevice_pending_event_delegate_,
            p2p_device);

  EXPECT_CALL(*p2p_device, GroupStarted(_)).Times(0);
  for (int i = 0; i < 10; i++)
    PostGroupStarted(properties);

  EXPECT_EQ(p2p_manager_->supplicant_primary_p2pdevice_pending_event_delegate_,
            p2p_device);
}

TEST_F(P2PManagerTest, GroupFinished) {
  KeyValueStore properties[3];
  RpcIdentifier interface_path[3];
  MockP2PDevice* p2p_device[3];

  // Create three groups
  for (int i = 0; i < 3; i++) {
    properties[i] = DefaultGroupFinishedProperties(kDefaultShillId + i);
    interface_path[i] = DefaultInterfacePath(kDefaultShillId + i);
    p2p_device[i] = new NiceMock<MockP2PDevice>(
        &manager_, LocalDevice::IfaceType::kP2PGO, "wlan0", 0,
        kDefaultShillId + i, WiFiPhy::Priority(0), event_cb_.Get());

    CreateP2PGroup(p2p_device[i]);

    EXPECT_EQ(p2p_manager_->p2p_group_owners_[kDefaultShillId + i],
              p2p_device[i]);
    EXPECT_EQ(
        p2p_manager_->supplicant_primary_p2pdevice_pending_event_delegate_,
        p2p_device[i]);

    // Start two of them
    if (i < 2) {
      EXPECT_CALL(*(p2p_device[i]), GroupStarted(properties[i])).Times(1);
      PostGroupStarted(properties[i]);

      EXPECT_EQ(
          p2p_manager_->supplicant_primary_p2pdevice_pending_event_delegate_,
          nullptr);
      EXPECT_EQ(p2p_manager_->supplicant_primary_p2pdevice_event_delegates_
                    [interface_path[i]],
                p2p_device[i]);
    }
  }

  // Finish the first one
  EXPECT_CALL(*p2p_device[0], GroupFinished(properties[0])).Times(1);
  EXPECT_CALL(*p2p_device[1], GroupFinished(_)).Times(0);
  EXPECT_CALL(*p2p_device[2], GroupFinished(_)).Times(0);
  PostGroupFinished(properties[0]);

  EXPECT_EQ(p2p_manager_->supplicant_primary_p2pdevice_pending_event_delegate_,
            p2p_device[2]);
  EXPECT_EQ(
      p2p_manager_
          ->supplicant_primary_p2pdevice_event_delegates_[interface_path[0]],
      nullptr);
  EXPECT_EQ(
      p2p_manager_
          ->supplicant_primary_p2pdevice_event_delegates_[interface_path[1]],
      p2p_device[1]);
  EXPECT_EQ(
      p2p_manager_
          ->supplicant_primary_p2pdevice_event_delegates_[interface_path[2]],
      nullptr);
}

TEST_F(P2PManagerTest, GroupFinished_BeforeStarted) {
  KeyValueStore properties = DefaultGroupFinishedProperties(kDefaultShillId);
  RpcIdentifier interface_path = properties.Get<RpcIdentifier>(
      WPASupplicant::kGroupFinishedPropertyInterfaceObject);
  MockP2PDevice* p2p_device = new NiceMock<MockP2PDevice>(
      &manager_, LocalDevice::IfaceType::kP2PGO, "wlan0", 0, kDefaultShillId,
      WiFiPhy::Priority(0), event_cb_.Get());

  CreateP2PGroup(p2p_device);

  EXPECT_EQ(p2p_manager_->p2p_group_owners_[kDefaultShillId], p2p_device);
  EXPECT_EQ(p2p_manager_->supplicant_primary_p2pdevice_pending_event_delegate_,
            p2p_device);

  EXPECT_CALL(*p2p_device, GroupFinished(properties)).Times(0);
  PostGroupFinished(properties);

  EXPECT_EQ(p2p_manager_->supplicant_primary_p2pdevice_pending_event_delegate_,
            p2p_device);
}

TEST_F(P2PManagerTest, GroupFinished_IgnoreDuplicates) {
  KeyValueStore properties = DefaultGroupFinishedProperties(kDefaultShillId);
  RpcIdentifier interface_path = properties.Get<RpcIdentifier>(
      WPASupplicant::kGroupFinishedPropertyInterfaceObject);
  MockP2PDevice* p2p_device = new NiceMock<MockP2PDevice>(
      &manager_, LocalDevice::IfaceType::kP2PGO, "wlan0", 0, kDefaultShillId,
      WiFiPhy::Priority(0), event_cb_.Get());

  CreateP2PGroup(p2p_device);

  EXPECT_EQ(p2p_manager_->p2p_group_owners_[kDefaultShillId], p2p_device);
  EXPECT_EQ(p2p_manager_->supplicant_primary_p2pdevice_pending_event_delegate_,
            p2p_device);

  EXPECT_CALL(*p2p_device, GroupStarted(properties)).Times(1);
  PostGroupStarted(properties);

  EXPECT_EQ(p2p_manager_->supplicant_primary_p2pdevice_pending_event_delegate_,
            nullptr);
  EXPECT_EQ(p2p_manager_
                ->supplicant_primary_p2pdevice_event_delegates_[interface_path],
            p2p_device);

  EXPECT_CALL(*p2p_device, GroupFinished(properties)).Times(1);
  PostGroupFinished(properties);

  EXPECT_EQ(p2p_manager_
                ->supplicant_primary_p2pdevice_event_delegates_[interface_path],
            nullptr);

  EXPECT_CALL(*p2p_device, GroupFinished(_)).Times(0);
  for (int i = 0; i < 10; i++)
    PostGroupFinished(properties);

  EXPECT_EQ(p2p_manager_
                ->supplicant_primary_p2pdevice_event_delegates_[interface_path],
            nullptr);
}

TEST_F(P2PManagerTest, GroupFinished_IgnoreMissingDevice) {
  KeyValueStore properties = DefaultGroupFinishedProperties(kDefaultShillId);
  RpcIdentifier interface_path = properties.Get<RpcIdentifier>(
      WPASupplicant::kGroupFinishedPropertyInterfaceObject);
  MockP2PDevice* p2p_device = new NiceMock<MockP2PDevice>(
      &manager_, LocalDevice::IfaceType::kP2PGO, "wlan0", 0, kDefaultShillId,
      WiFiPhy::Priority(0), event_cb_.Get());

  CreateP2PGroup(p2p_device);

  EXPECT_EQ(p2p_manager_->p2p_group_owners_[kDefaultShillId], p2p_device);
  EXPECT_EQ(p2p_manager_->supplicant_primary_p2pdevice_pending_event_delegate_,
            p2p_device);

  EXPECT_CALL(*p2p_device, GroupStarted(properties)).Times(1);
  PostGroupStarted(properties);

  EXPECT_EQ(p2p_manager_->supplicant_primary_p2pdevice_pending_event_delegate_,
            nullptr);
  EXPECT_EQ(p2p_manager_
                ->supplicant_primary_p2pdevice_event_delegates_[interface_path],
            p2p_device);

  EXPECT_CALL(*p2p_device, GroupFinished(_)).Times(0);
  for (int i = 0; i < 10; i++)
    PostGroupFinished(kDefaultShillId + 1);

  EXPECT_EQ(p2p_manager_->supplicant_primary_p2pdevice_pending_event_delegate_,
            nullptr);
  EXPECT_EQ(p2p_manager_
                ->supplicant_primary_p2pdevice_event_delegates_[interface_path],
            p2p_device);

  interface_path = DefaultInterfacePath(kDefaultShillId + 1);
  EXPECT_EQ(p2p_manager_
                ->supplicant_primary_p2pdevice_event_delegates_[interface_path],
            nullptr);
}

TEST_F(P2PManagerTest, GroupFinished_IgnoreMissingProperties) {
  KeyValueStore properties; /* empty properties */
  MockP2PDevice* p2p_device = new NiceMock<MockP2PDevice>(
      &manager_, LocalDevice::IfaceType::kP2PGO, "wlan0", 0, kDefaultShillId,
      WiFiPhy::Priority(0), event_cb_.Get());

  CreateP2PGroup(p2p_device);

  EXPECT_EQ(p2p_manager_->p2p_group_owners_[kDefaultShillId], p2p_device);
  EXPECT_EQ(p2p_manager_->supplicant_primary_p2pdevice_pending_event_delegate_,
            p2p_device);

  EXPECT_CALL(*p2p_device, GroupFinished(_)).Times(0);
  for (int i = 0; i < 10; i++)
    PostGroupFinished(properties);

  EXPECT_EQ(p2p_manager_->supplicant_primary_p2pdevice_pending_event_delegate_,
            p2p_device);
}

TEST_F(P2PManagerTest, GroupFormationFailure) {
  std::string reason = "Unknown";
  MockP2PDevice* p2p_device = new NiceMock<MockP2PDevice>(
      &manager_, LocalDevice::IfaceType::kP2PGO, "wlan0", 0, kDefaultShillId,
      WiFiPhy::Priority(0), event_cb_.Get());

  CreateP2PGroup(p2p_device);

  EXPECT_EQ(p2p_manager_->p2p_group_owners_[kDefaultShillId], p2p_device);
  EXPECT_EQ(p2p_manager_->supplicant_primary_p2pdevice_pending_event_delegate_,
            p2p_device);

  EXPECT_CALL(*p2p_device, GroupFormationFailure(reason)).Times(1);
  PostGroupFormationFailure(reason);

  EXPECT_EQ(p2p_manager_->supplicant_primary_p2pdevice_pending_event_delegate_,
            nullptr);
}

TEST_F(P2PManagerTest, GroupFormationFailure_IgnoreDuplicates) {
  std::string reason = "Unknown";
  MockP2PDevice* p2p_device = new NiceMock<MockP2PDevice>(
      &manager_, LocalDevice::IfaceType::kP2PGO, "wlan0", 0, kDefaultShillId,
      WiFiPhy::Priority(0), event_cb_.Get());

  CreateP2PGroup(p2p_device);

  EXPECT_EQ(p2p_manager_->p2p_group_owners_[kDefaultShillId], p2p_device);
  EXPECT_EQ(p2p_manager_->supplicant_primary_p2pdevice_pending_event_delegate_,
            p2p_device);

  EXPECT_CALL(*p2p_device, GroupFormationFailure(reason)).Times(1);
  PostGroupFormationFailure(reason);

  EXPECT_EQ(p2p_manager_->supplicant_primary_p2pdevice_pending_event_delegate_,
            nullptr);

  EXPECT_CALL(*p2p_device, GroupFormationFailure(_)).Times(0);
  for (int i = 0; i < 10; i++)
    PostGroupFormationFailure(reason);

  EXPECT_EQ(p2p_manager_->supplicant_primary_p2pdevice_pending_event_delegate_,
            nullptr);
}

TEST_F(P2PManagerTest, GroupFormationFailure_IgnoreMissingDevice) {
  std::string reason = "Unknown";
  KeyValueStore properties = DefaultGroupStartedProperties(kDefaultShillId);
  RpcIdentifier interface_path = properties.Get<RpcIdentifier>(
      WPASupplicant::kGroupStartedPropertyInterfaceObject);
  MockP2PDevice* p2p_device = new NiceMock<MockP2PDevice>(
      &manager_, LocalDevice::IfaceType::kP2PGO, "wlan0", 0, kDefaultShillId,
      WiFiPhy::Priority(0), event_cb_.Get());

  CreateP2PGroup(p2p_device);

  EXPECT_EQ(p2p_manager_->p2p_group_owners_[kDefaultShillId], p2p_device);
  EXPECT_EQ(p2p_manager_->supplicant_primary_p2pdevice_pending_event_delegate_,
            p2p_device);

  EXPECT_CALL(*p2p_device, GroupStarted(properties)).Times(1);
  PostGroupStarted(properties);

  EXPECT_EQ(p2p_manager_->supplicant_primary_p2pdevice_pending_event_delegate_,
            nullptr);
  EXPECT_EQ(p2p_manager_
                ->supplicant_primary_p2pdevice_event_delegates_[interface_path],
            p2p_device);

  EXPECT_CALL(*p2p_device, GroupFormationFailure(_)).Times(0);
  for (int i = 0; i < 10; i++)
    PostGroupFormationFailure(reason);

  EXPECT_EQ(p2p_manager_->supplicant_primary_p2pdevice_pending_event_delegate_,
            nullptr);
  EXPECT_EQ(p2p_manager_
                ->supplicant_primary_p2pdevice_event_delegates_[interface_path],
            p2p_device);
}

TEST_F(P2PManagerTest, CreateDeviceRejected_CreateGroup) {
  KeyValueStore response_dict;
  KeyValueStore properties;
  properties.Set<int32_t>(kP2PDevicePriority, WiFiPhy::Priority(1));
  base::MockOnceCallback<void(KeyValueStore)> cb;
  EXPECT_CALL(*wifi_provider_, RequestLocalDeviceCreation)
      .WillOnce(Return(false));
  EXPECT_CALL(cb, Run(_)).WillOnce(SaveArg<0>(&response_dict));
  p2p_manager_->CreateP2PGroup(cb.Get(), properties);
  DispatchPendingEvents();
  EXPECT_TRUE(IsActionTimerCancelled());
  ASSERT_EQ(response_dict.Get<std::string>(kP2PResultCode),
            kCreateP2PGroupResultConcurrencyNotSupported);
}

TEST_F(P2PManagerTest, CreateDeviceRejected_Connect) {
  KeyValueStore response_dict;
  KeyValueStore properties;
  properties.Set<int32_t>(kP2PDevicePriority, WiFiPhy::Priority(1));
  properties.Set<std::string>(kP2PDeviceSSID, "DIRECT-ab");
  properties.Set<std::string>(kP2PDevicePassphrase, "test0000");
  properties.Set<int32_t>(kP2PDeviceFrequency, 1234);
  base::MockOnceCallback<void(KeyValueStore)> cb;
  EXPECT_CALL(*wifi_provider_, RequestLocalDeviceCreation)
      .WillOnce(Return(false));
  EXPECT_CALL(cb, Run(_)).WillOnce(SaveArg<0>(&response_dict));
  p2p_manager_->ConnectToP2PGroup(cb.Get(), properties);
  DispatchPendingEvents();
  EXPECT_TRUE(IsActionTimerCancelled());
  ASSERT_EQ(response_dict.Get<std::string>(kP2PResultCode),
            kConnectToP2PGroupResultConcurrencyNotSupported);
}

TEST_F(P2PManagerTest, DeviceCreationFailed_CreateGroup) {
  KeyValueStore response_dict;
  KeyValueStore properties;
  properties.Set<int32_t>(kP2PDevicePriority, WiFiPhy::Priority(1));
  base::MockOnceCallback<void(KeyValueStore)> cb;

  EXPECT_CALL(*wifi_provider_, CreateP2PDevice)
      .WillOnce([](Unused, Unused, Unused, Unused, Unused,
                   base::OnceCallback<void()> fail_cb) {
        std::move(fail_cb).Run();
        return true;
      });
  EXPECT_CALL(*wifi_provider_, RequestLocalDeviceCreation(_, _, _))
      .WillOnce([](Unused, Unused, base::OnceClosure create_device_cb) {
        std::move(create_device_cb).Run();
        return true;
      });
  EXPECT_CALL(cb, Run(_)).WillOnce(SaveArg<0>(&response_dict));
  p2p_manager_->CreateP2PGroup(cb.Get(), properties);
  DispatchPendingEvents();
  EXPECT_TRUE(IsActionTimerCancelled());
  ASSERT_EQ(response_dict.Get<std::string>(kP2PResultCode),
            kCreateP2PGroupResultOperationFailed);
}

TEST_F(P2PManagerTest, DeviceCreationFailed_Connect) {
  KeyValueStore response_dict;
  KeyValueStore properties;
  properties.Set<std::string>(kP2PDeviceSSID, "DIRECT-ab");
  properties.Set<std::string>(kP2PDevicePassphrase, "test0000");
  properties.Set<int32_t>(kP2PDeviceFrequency, 1234);
  properties.Set<int32_t>(kP2PDevicePriority, WiFiPhy::Priority(1));
  base::MockOnceCallback<void(KeyValueStore)> cb;

  EXPECT_CALL(*wifi_provider_, CreateP2PDevice)
      .WillOnce([](Unused, Unused, Unused, Unused, Unused,
                   base::OnceCallback<void()> fail_cb) {
        std::move(fail_cb).Run();
        return true;
      });
  EXPECT_CALL(*wifi_provider_, RequestLocalDeviceCreation(_, _, _))
      .WillOnce([](Unused, Unused, base::OnceClosure create_device_cb) {
        std::move(create_device_cb).Run();
        return true;
      });
  EXPECT_CALL(cb, Run(_)).WillOnce(SaveArg<0>(&response_dict));
  p2p_manager_->ConnectToP2PGroup(cb.Get(), properties);
  OnDeviceCreationFailed(LocalDevice::IfaceType::kP2PGO);
  DispatchPendingEvents();
  EXPECT_TRUE(IsActionTimerCancelled());
  ASSERT_EQ(response_dict.Get<std::string>(kP2PResultCode),
            kConnectToP2PGroupResultOperationFailed);
}

TEST_F(P2PManagerTest, StartTimeout_GOStarting) {
  MockP2PDevice* p2p_device = new NiceMock<MockP2PDevice>(
      &manager_, LocalDevice::IfaceType::kP2PGO, "wlan0", 0, 0,
      WiFiPhy::Priority(0), event_cb_.Get());
  KeyValueStore properties;
  KeyValueStore response_dict;
  base::MockOnceCallback<void(KeyValueStore)> cb;
  properties.Set<int32_t>(kP2PDevicePriority, 1);
  SetDefaultDeviceLinkName(p2p_device);

  EXPECT_CALL(*wifi_provider_, CreateP2PDevice)
      .WillOnce(
          [p2p_device](Unused, Unused, Unused, Unused,
                       base::OnceCallback<void(P2PDeviceRefPtr)> success_cb,
                       Unused) {
            std::move(success_cb).Run(p2p_device);
            return true;
          });
  EXPECT_CALL(*wifi_provider_, RequestLocalDeviceCreation(_, _, _))
      .WillOnce([](Unused, Unused, base::OnceClosure create_device_cb) {
        std::move(create_device_cb).Run();
        return true;
      });

  ON_CALL(*p2p_device, CreateGroup(_)).WillByDefault(Return(true));
  ON_CALL(cb, Run(_)).WillByDefault(SaveArg<0>(&response_dict));

  p2p_manager_->CreateP2PGroup(cb.Get(), properties);
  FastForward(true);
  EXPECT_TRUE(IsActionTimerCancelled());
  ASSERT_EQ(response_dict.Get<std::string>(kP2PResultCode),
            kCreateP2PGroupResultTimeout);
  EXPECT_EQ(p2p_manager_->supplicant_primary_p2pdevice_pending_event_delegate_,
            nullptr);
}

TEST_F(P2PManagerTest, StartTimeout_GOConfiguring) {
  MockP2PDevice* p2p_device = new NiceMock<MockP2PDevice>(
      &manager_, LocalDevice::IfaceType::kP2PGO, "wlan0", 0, 0,
      WiFiPhy::Priority(0), event_cb_.Get());
  KeyValueStore properties;
  KeyValueStore response_dict;
  base::MockOnceCallback<void(KeyValueStore)> cb;
  properties.Set<int32_t>(kP2PDevicePriority, 1);
  SetDefaultDeviceLinkName(p2p_device);

  EXPECT_CALL(*wifi_provider_, CreateP2PDevice)
      .WillOnce(
          [p2p_device](Unused, Unused, Unused, Unused,
                       base::OnceCallback<void(P2PDeviceRefPtr)> success_cb,
                       Unused) {
            std::move(success_cb).Run(p2p_device);
            return true;
          });
  EXPECT_CALL(*wifi_provider_, RequestLocalDeviceCreation(_, _, _))
      .WillOnce([](Unused, Unused, base::OnceClosure create_device_cb) {
        std::move(create_device_cb).Run();
        return true;
      });
  ON_CALL(*p2p_device, CreateGroup(_)).WillByDefault(Return(true));
  ON_CALL(cb, Run(_)).WillByDefault(SaveArg<0>(&response_dict));

  p2p_manager_->CreateP2PGroup(cb.Get(), properties);
  OnP2PDeviceEvent(LocalDevice::DeviceEvent::kLinkUp, p2p_device);
  FastForward(true);
  EXPECT_TRUE(IsActionTimerCancelled());
  ASSERT_EQ(response_dict.Get<std::string>(kP2PResultCode),
            kCreateP2PGroupResultTimeout);
  EXPECT_EQ(p2p_manager_->supplicant_primary_p2pdevice_pending_event_delegate_,
            nullptr);
}

TEST_F(P2PManagerTest, StartTimeout_GOActive) {
  MockP2PDevice* p2p_device = new NiceMock<MockP2PDevice>(
      &manager_, LocalDevice::IfaceType::kP2PGO, "wlan0", 0, 0,
      WiFiPhy::Priority(0), event_cb_.Get());
  KeyValueStore properties;
  KeyValueStore response_dict;
  base::MockOnceCallback<void(KeyValueStore)> cb;
  properties.Set<int32_t>(kP2PDevicePriority, 1);
  SetDefaultDeviceLinkName(p2p_device);

  EXPECT_CALL(*wifi_provider_, CreateP2PDevice)
      .WillOnce(
          [p2p_device](Unused, Unused, Unused, Unused,
                       base::OnceCallback<void(P2PDeviceRefPtr)> success_cb,
                       Unused) {
            std::move(success_cb).Run(p2p_device);
            return true;
          });
  EXPECT_CALL(*wifi_provider_, RequestLocalDeviceCreation(_, _, _))
      .WillOnce([](Unused, Unused, base::OnceClosure create_device_cb) {
        std::move(create_device_cb).Run();
        return true;
      });
  ON_CALL(*p2p_device, CreateGroup(_)).WillByDefault(Return(true));
  ON_CALL(cb, Run(_)).WillByDefault(SaveArg<0>(&response_dict));

  p2p_manager_->CreateP2PGroup(cb.Get(), properties);
  OnP2PDeviceEvent(LocalDevice::DeviceEvent::kNetworkUp, p2p_device);
  DispatchPendingEvents();
  EXPECT_TRUE(IsActionTimerCancelled());
  FastForward(true);
  EXPECT_TRUE(IsActionTimerCancelled());
  ASSERT_EQ(response_dict.Get<std::string>(kP2PResultCode),
            kCreateP2PGroupResultSuccess);
}

TEST_F(P2PManagerTest, StartTimeout_ClientAssociating) {
  MockP2PDevice* p2p_device = new NiceMock<MockP2PDevice>(
      &manager_, LocalDevice::IfaceType::kP2PClient, "wlan0", 0, 0,
      WiFiPhy::Priority(0), event_cb_.Get());
  KeyValueStore properties;
  KeyValueStore response_dict;
  base::MockOnceCallback<void(KeyValueStore)> cb;
  properties.Set<std::string>(kP2PDeviceSSID, "DIRECT-ab");
  properties.Set<std::string>(kP2PDevicePassphrase, "test0000");
  properties.Set<int32_t>(kP2PDeviceFrequency, 1234);
  properties.Set<int32_t>(kP2PDevicePriority, 1);
  SetDefaultDeviceLinkName(p2p_device);

  EXPECT_CALL(*wifi_provider_, CreateP2PDevice)
      .WillOnce(
          [p2p_device](Unused, Unused, Unused, Unused,
                       base::OnceCallback<void(P2PDeviceRefPtr)> success_cb,
                       Unused) {
            std::move(success_cb).Run(p2p_device);
            return true;
          });
  EXPECT_CALL(*wifi_provider_, RequestLocalDeviceCreation(_, _, _))
      .WillOnce([](Unused, Unused, base::OnceClosure create_device_cb) {
        std::move(create_device_cb).Run();
        return true;
      });
  ON_CALL(*p2p_device, Connect(_)).WillByDefault(Return(true));
  ON_CALL(cb, Run(_)).WillByDefault(SaveArg<0>(&response_dict));

  p2p_manager_->ConnectToP2PGroup(cb.Get(), properties);
  FastForward(true);
  EXPECT_TRUE(IsActionTimerCancelled());
  ASSERT_EQ(response_dict.Get<std::string>(kP2PResultCode),
            kConnectToP2PGroupResultTimeout);
  EXPECT_EQ(p2p_manager_->supplicant_primary_p2pdevice_pending_event_delegate_,
            nullptr);
}

TEST_F(P2PManagerTest, StartTimeout_ClientConfiguring) {
  MockP2PDevice* p2p_device = new NiceMock<MockP2PDevice>(
      &manager_, LocalDevice::IfaceType::kP2PClient, "wlan0", 0, 0,
      WiFiPhy::Priority(0), event_cb_.Get());
  KeyValueStore properties;
  KeyValueStore response_dict;
  base::MockOnceCallback<void(KeyValueStore)> cb;
  properties.Set<std::string>(kP2PDeviceSSID, "DIRECT-ab");
  properties.Set<std::string>(kP2PDevicePassphrase, "test0000");
  properties.Set<int32_t>(kP2PDeviceFrequency, 1234);
  properties.Set<int32_t>(kP2PDevicePriority, 1);
  SetDefaultDeviceLinkName(p2p_device);

  EXPECT_CALL(*wifi_provider_, CreateP2PDevice)
      .WillOnce(
          [p2p_device](Unused, Unused, Unused, Unused,
                       base::OnceCallback<void(P2PDeviceRefPtr)> success_cb,
                       Unused) {
            std::move(success_cb).Run(p2p_device);
            return true;
          });
  EXPECT_CALL(*wifi_provider_, RequestLocalDeviceCreation(_, _, _))
      .WillOnce([](Unused, Unused, base::OnceClosure create_device_cb) {
        std::move(create_device_cb).Run();
        return true;
      });
  ON_CALL(*p2p_device, Connect(_)).WillByDefault(Return(true));
  ON_CALL(cb, Run(_)).WillByDefault(SaveArg<0>(&response_dict));

  p2p_manager_->ConnectToP2PGroup(cb.Get(), properties);
  OnP2PDeviceEvent(LocalDevice::DeviceEvent::kLinkUp, p2p_device);
  FastForward(true);
  EXPECT_TRUE(IsActionTimerCancelled());
  ASSERT_EQ(response_dict.Get<std::string>(kP2PResultCode),
            kConnectToP2PGroupResultTimeout);
  EXPECT_EQ(p2p_manager_->supplicant_primary_p2pdevice_pending_event_delegate_,
            nullptr);
}

TEST_F(P2PManagerTest, StartTimeout_ClientConnected) {
  MockP2PDevice* p2p_device = new NiceMock<MockP2PDevice>(
      &manager_, LocalDevice::IfaceType::kP2PClient, "wlan0", 0, 0,
      WiFiPhy::Priority(0), event_cb_.Get());
  KeyValueStore properties;
  KeyValueStore response_dict;
  base::MockOnceCallback<void(KeyValueStore)> cb;
  properties.Set<std::string>(kP2PDeviceSSID, "DIRECT-ab");
  properties.Set<std::string>(kP2PDevicePassphrase, "test0000");
  properties.Set<int32_t>(kP2PDeviceFrequency, 1234);
  properties.Set<int32_t>(kP2PDevicePriority, 1);
  SetDefaultDeviceLinkName(p2p_device);

  EXPECT_CALL(*wifi_provider_, CreateP2PDevice)
      .WillOnce(
          [p2p_device](Unused, Unused, Unused, Unused,
                       base::OnceCallback<void(P2PDeviceRefPtr)> success_cb,
                       Unused) {
            std::move(success_cb).Run(p2p_device);
            return true;
          });
  EXPECT_CALL(*wifi_provider_, RequestLocalDeviceCreation(_, _, _))
      .WillOnce([](Unused, Unused, base::OnceClosure create_device_cb) {
        std::move(create_device_cb).Run();
        return true;
      });
  ON_CALL(*p2p_device, Connect(_)).WillByDefault(Return(true));
  ON_CALL(cb, Run(_)).WillByDefault(SaveArg<0>(&response_dict));

  p2p_manager_->ConnectToP2PGroup(cb.Get(), properties);
  OnP2PDeviceEvent(LocalDevice::DeviceEvent::kNetworkUp, p2p_device);
  DispatchPendingEvents();
  EXPECT_TRUE(IsActionTimerCancelled());
  FastForward(true);
  EXPECT_TRUE(IsActionTimerCancelled());
  ASSERT_EQ(response_dict.Get<std::string>(kP2PResultCode),
            kConnectToP2PGroupResultSuccess);
}

TEST_F(P2PManagerTest, StopTimeout_GOStopping) {
  MockP2PDevice* p2p_device = new NiceMock<MockP2PDevice>(
      &manager_, LocalDevice::IfaceType::kP2PGO, "wlan0", 0, 0,
      WiFiPhy::Priority(0), event_cb_.Get());
  KeyValueStore properties;
  KeyValueStore response_dict;
  base::MockOnceCallback<void(KeyValueStore)> cb;
  properties.Set<int32_t>(kP2PDevicePriority, 1);
  SetDefaultDeviceLinkName(p2p_device);

  EXPECT_CALL(*wifi_provider_, CreateP2PDevice)
      .WillOnce(
          [p2p_device](Unused, Unused, Unused, Unused,
                       base::OnceCallback<void(P2PDeviceRefPtr)> success_cb,
                       Unused) {
            std::move(success_cb).Run(p2p_device);
            return true;
          });
  EXPECT_CALL(*wifi_provider_, RequestLocalDeviceCreation(_, _, _))
      .WillOnce([](Unused, Unused, base::OnceClosure create_device_cb) {
        std::move(create_device_cb).Run();
        return true;
      });
  ON_CALL(*p2p_device, CreateGroup(_)).WillByDefault(Return(true));
  ON_CALL(cb, Run(_)).WillByDefault(SaveArg<0>(&response_dict));

  p2p_manager_->CreateP2PGroup(cb.Get(), properties);
  OnP2PDeviceEvent(LocalDevice::DeviceEvent::kNetworkUp, p2p_device);
  DispatchPendingEvents();
  ASSERT_EQ(response_dict.Get<std::string>(kP2PResultCode),
            kCreateP2PGroupResultSuccess);
  int32_t expected_shill_id = response_dict.Get<int32_t>(kP2PDeviceShillID);

  ON_CALL(*p2p_device, RemoveGroup()).WillByDefault(Return(true));
  p2p_manager_->DestroyP2PGroup(cb.Get(), expected_shill_id);
  FastForward(false);
  EXPECT_TRUE(IsActionTimerCancelled());
  ASSERT_EQ(response_dict.Get<std::string>(kP2PResultCode),
            kDestroyP2PGroupResultTimeout);
}

TEST_F(P2PManagerTest, StopTimeout_ClientDisconnecting) {
  MockP2PDevice* p2p_device = new NiceMock<MockP2PDevice>(
      &manager_, LocalDevice::IfaceType::kP2PClient, "wlan0", 0, 0,
      WiFiPhy::Priority(0), event_cb_.Get());
  KeyValueStore properties;
  KeyValueStore response_dict;
  base::MockOnceCallback<void(KeyValueStore)> cb;
  properties.Set<std::string>(kP2PDeviceSSID, "DIRECT-ab");
  properties.Set<std::string>(kP2PDevicePassphrase, "test0000");
  properties.Set<int32_t>(kP2PDeviceFrequency, 1234);
  properties.Set<int32_t>(kP2PDevicePriority, 1);
  SetDefaultDeviceLinkName(p2p_device);

  EXPECT_CALL(*wifi_provider_, CreateP2PDevice)
      .WillOnce(
          [p2p_device](Unused, Unused, Unused, Unused,
                       base::OnceCallback<void(P2PDeviceRefPtr)> success_cb,
                       Unused) {
            std::move(success_cb).Run(p2p_device);
            return true;
          });
  EXPECT_CALL(*wifi_provider_, RequestLocalDeviceCreation(_, _, _))
      .WillOnce([](Unused, Unused, base::OnceClosure create_device_cb) {
        std::move(create_device_cb).Run();
        return true;
      });
  ON_CALL(*p2p_device, Connect(_)).WillByDefault(Return(true));
  ON_CALL(cb, Run(_)).WillByDefault(SaveArg<0>(&response_dict));

  p2p_manager_->ConnectToP2PGroup(cb.Get(), properties);
  OnP2PDeviceEvent(LocalDevice::DeviceEvent::kNetworkUp, p2p_device);
  DispatchPendingEvents();
  ASSERT_EQ(response_dict.Get<std::string>(kP2PResultCode),
            kConnectToP2PGroupResultSuccess);
  int32_t expected_shill_id = response_dict.Get<int32_t>(kP2PDeviceShillID);

  ON_CALL(*p2p_device, Disconnect()).WillByDefault(Return(true));
  p2p_manager_->DisconnectFromP2PGroup(cb.Get(), expected_shill_id);
  FastForward(false);
  EXPECT_TRUE(IsActionTimerCancelled());
  ASSERT_EQ(response_dict.Get<std::string>(kP2PResultCode),
            kDisconnectFromP2PGroupResultTimeout);
}

}  // namespace shill
