// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/wifi/p2p_manager.h"

#include <map>
#include <string>

#include <base/files/scoped_temp_dir.h>
#include <base/test/mock_callback.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "shill/mock_control.h"
#include "shill/mock_manager.h"
#include "shill/mock_metrics.h"
#include "shill/test_event_dispatcher.h"
#include "shill/wifi/mock_p2p_device.h"
#include "shill/wifi/mock_wifi_phy.h"
#include "shill/wifi/mock_wifi_provider.h"

using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::SaveArg;
using testing::StrictMock;
using testing::Test;

namespace shill {

class P2PManagerTest : public testing::Test {
 public:
  P2PManagerTest()
      : temp_dir_(MakeTempDir()),
        path_(temp_dir_.GetPath().value()),
        manager_(
            &control_interface_, &dispatcher_, &metrics_, path_, path_, path_),
        wifi_provider_(new NiceMock<MockWiFiProvider>(&manager_)),
        p2p_manager_(wifi_provider_->p2p_manager()) {
    // Replace the Manager's WiFi provider with a mock.
    manager_.wifi_provider_.reset(wifi_provider_);
    // Update the Manager's map from technology to provider.
    manager_.UpdateProviderMapping();
  }

  ~P2PManagerTest() override = default;

  void SetAllowed(P2PManager* p2p_manager, bool allowed) {
    Error error;
    PropertyStore store;
    p2p_manager->InitPropertyStore(&store);
    store.SetBoolProperty(kP2PAllowedProperty, allowed, &error);
    EXPECT_TRUE(error.IsSuccess());
  }

  base::ScopedTempDir MakeTempDir() {
    base::ScopedTempDir temp_dir;
    EXPECT_TRUE(temp_dir.CreateUniqueTempDir());
    return temp_dir;
  }

  void DispatchPendingEvents() { dispatcher_.DispatchPendingEvents(); }

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
};

TEST_F(P2PManagerTest, SetP2PAllowed) {
  SetAllowed(p2p_manager_, true);
  EXPECT_EQ(p2p_manager_->allowed_, true);
  SetAllowed(p2p_manager_, false);
  EXPECT_EQ(p2p_manager_->allowed_, false);
}

TEST_F(P2PManagerTest, ConnectAndDisconnectClient) {
  KeyValueStore properties;
  properties.Set<std::string>(kP2PDeviceSSID, "DIRECT-ab");
  properties.Set<std::string>(kP2PDevicePassphrase, "test0000");
  properties.Set<uint32_t>(kP2PDeviceFrequency, 1234);
  MockP2PDevice* p2p_device =
      new NiceMock<MockP2PDevice>(&manager_, LocalDevice::IfaceType::kP2PClient,
                                  "wlan0", 0, 0, event_cb_.Get());
  base::MockOnceCallback<void(KeyValueStore)> cb;
  KeyValueStore response_dict;
  uint32_t expected_shill_id = p2p_manager_->next_unique_id_;

  EXPECT_CALL(*wifi_provider_, CreateP2PDevice(_, _, _))
      .WillOnce(Return(p2p_device));
  EXPECT_CALL(cb, Run(_)).WillOnce(SaveArg<0>(&response_dict));
  EXPECT_CALL(*p2p_device, Connect(_)).WillOnce(Return(true));
  p2p_manager_->ConnectToP2PGroup(cb.Get(), properties);
  DispatchPendingEvents();
  ASSERT_EQ(response_dict.Get<std::string>(kP2PResultCode),
            kConnectToP2PGroupResultSuccess);
  ASSERT_EQ(p2p_manager_->p2p_clients_[expected_shill_id], p2p_device);

  EXPECT_CALL(cb, Run(_)).WillOnce(SaveArg<0>(&response_dict));
  p2p_manager_->DisconnectFromP2PGroup(cb.Get(), expected_shill_id);
  DispatchPendingEvents();
  ASSERT_EQ(response_dict.Get<std::string>(kP2PResultCode),
            kDisconnectFromP2PGroupResultSuccess);
  ASSERT_EQ(p2p_manager_->p2p_clients_.count(expected_shill_id), 0);
}

TEST_F(P2PManagerTest, CreateAndDestroyGroup) {
  KeyValueStore properties;
  properties.Set<std::string>(kP2PDeviceSSID, "DIRECT-ab");
  properties.Set<std::string>(kP2PDevicePassphrase, "test0000");
  properties.Set<uint32_t>(kP2PDeviceFrequency, 1234);
  MockP2PDevice* p2p_device =
      new NiceMock<MockP2PDevice>(&manager_, LocalDevice::IfaceType::kP2PGO,
                                  "wlan0", 0, 0, event_cb_.Get());
  base::MockOnceCallback<void(KeyValueStore)> cb;
  KeyValueStore response_dict;
  uint32_t expected_shill_id = p2p_manager_->next_unique_id_;

  EXPECT_CALL(*wifi_provider_, CreateP2PDevice(_, _, _))
      .WillOnce(Return(p2p_device));
  EXPECT_CALL(cb, Run(_)).WillOnce(SaveArg<0>(&response_dict));
  EXPECT_CALL(*p2p_device, CreateGroup(_)).WillOnce(Return(true));
  p2p_manager_->CreateP2PGroup(cb.Get(), properties);
  DispatchPendingEvents();
  ASSERT_EQ(response_dict.Get<std::string>(kP2PResultCode),
            kCreateP2PGroupResultSuccess);
  ASSERT_EQ(p2p_manager_->p2p_group_owners_[expected_shill_id], p2p_device);

  EXPECT_CALL(cb, Run(_)).WillOnce(SaveArg<0>(&response_dict));
  p2p_manager_->DestroyP2PGroup(cb.Get(), expected_shill_id);
  DispatchPendingEvents();
  ASSERT_EQ(response_dict.Get<std::string>(kP2PResultCode),
            kDestroyP2PGroupResultSuccess);
  ASSERT_EQ(p2p_manager_->p2p_group_owners_.count(expected_shill_id), 0);
}

TEST_F(P2PManagerTest, DisconnectWithoutConnect) {
  base::MockOnceCallback<void(KeyValueStore)> cb;
  KeyValueStore response_dict;
  uint32_t shill_id = 0;

  EXPECT_CALL(cb, Run(_)).WillOnce(SaveArg<0>(&response_dict));
  p2p_manager_->DisconnectFromP2PGroup(cb.Get(), shill_id);
  DispatchPendingEvents();
  ASSERT_EQ(response_dict.Get<std::string>(kP2PResultCode),
            kDisconnectFromP2PGroupResultNotConnected);
}

TEST_F(P2PManagerTest, DestroyWithoutCreate) {
  base::MockOnceCallback<void(KeyValueStore)> cb;
  KeyValueStore response_dict;
  uint32_t shill_id = 0;

  EXPECT_CALL(cb, Run(_)).WillOnce(SaveArg<0>(&response_dict));
  p2p_manager_->DestroyP2PGroup(cb.Get(), shill_id);
  DispatchPendingEvents();
  ASSERT_EQ(response_dict.Get<std::string>(kP2PResultCode),
            kDestroyP2PGroupResultNoGroup);
}

TEST_F(P2PManagerTest, ShillIDs) {
  KeyValueStore properties;
  properties.Set<std::string>(kP2PDevicePassphrase, "test0000");
  properties.Set<uint32_t>(kP2PDeviceFrequency, 1234);
  uint32_t current_id = p2p_manager_->next_unique_id_;
  std::string ssid;
  base::MockOnceCallback<void(KeyValueStore)> cb;

  for (int i = 0; i < 10; i++) {
    MockP2PDevice* p2p_device = new NiceMock<MockP2PDevice>(
        &manager_, LocalDevice::IfaceType::kP2PClient, "wlan0", 0, current_id,
        event_cb_.Get());
    // Each client needs a unique SSID.
    std::string ssid = "DIRECT-ab-" + std::to_string(i);
    properties.Set<std::string>(kP2PDeviceSSID, ssid);
    EXPECT_CALL(*wifi_provider_, CreateP2PDevice(_, _, _))
        .WillOnce(Return(p2p_device));
    EXPECT_CALL(*p2p_device, Connect(_)).WillOnce(Return(true));
    p2p_manager_->ConnectToP2PGroup(cb.Get(), properties);
    EXPECT_EQ(p2p_manager_->p2p_clients_[current_id], p2p_device);
    current_id++;
  }

  for (int i = 0; i < 10; i++) {
    MockP2PDevice* p2p_device =
        new NiceMock<MockP2PDevice>(&manager_, LocalDevice::IfaceType::kP2PGO,
                                    "wlan0", 0, current_id, event_cb_.Get());
    // Each group owner needs a unique SSID.
    std::string ssid = "DIRECT-ab-" + std::to_string(i);
    properties.Set<std::string>(kP2PDeviceSSID, ssid);
    EXPECT_CALL(*wifi_provider_, CreateP2PDevice(_, _, _))
        .WillOnce(Return(p2p_device));
    EXPECT_CALL(*p2p_device, CreateGroup(_)).WillOnce(Return(true));
    p2p_manager_->CreateP2PGroup(cb.Get(), properties);
    EXPECT_EQ(p2p_manager_->p2p_group_owners_[current_id], p2p_device);
    current_id++;
  }
}

TEST_F(P2PManagerTest, MissingArgs_CreateGroup) {
  KeyValueStore properties;
  MockP2PDevice* p2p_device =
      new NiceMock<MockP2PDevice>(&manager_, LocalDevice::IfaceType::kP2PGO,
                                  "wlan0", 0, 0, event_cb_.Get());
  base::MockOnceCallback<void(KeyValueStore)> cb;
  KeyValueStore response_dict;
  uint32_t expected_shill_id = p2p_manager_->next_unique_id_;

  EXPECT_CALL(*wifi_provider_, CreateP2PDevice(_, _, _))
      .WillOnce(Return(p2p_device));
  EXPECT_CALL(cb, Run(_)).WillOnce(SaveArg<0>(&response_dict));
  EXPECT_CALL(*p2p_device, CreateGroup(_)).WillOnce(Return(true));
  p2p_manager_->CreateP2PGroup(cb.Get(), properties);
  DispatchPendingEvents();
  ASSERT_EQ(response_dict.Get<std::string>(kP2PResultCode),
            kCreateP2PGroupResultSuccess);
  ASSERT_EQ(p2p_manager_->p2p_group_owners_[expected_shill_id], p2p_device);
}

TEST_F(P2PManagerTest, MissingArgs_ConnectClient) {
  KeyValueStore properties;
  base::MockOnceCallback<void(KeyValueStore)> cb;
  KeyValueStore response_dict;
  uint32_t expected_shill_id = p2p_manager_->next_unique_id_;

  EXPECT_CALL(*wifi_provider_, CreateP2PDevice(_, _, _)).Times(0);
  EXPECT_CALL(cb, Run(_)).WillOnce(SaveArg<0>(&response_dict));
  p2p_manager_->ConnectToP2PGroup(cb.Get(), properties);
  DispatchPendingEvents();
  ASSERT_EQ(response_dict.Get<std::string>(kP2PResultCode),
            kConnectToP2PGroupResultInvalidArguments);
  ASSERT_EQ(p2p_manager_->p2p_clients_.count(expected_shill_id), 0);
}

}  // namespace shill
