// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/wifi/hotspot_device.h"

#include <memory>
#include <string>

#include <base/memory/ref_counted.h>
#include <base/test/mock_callback.h>
#include <gmock/gmock.h>

#include "shill/error.h"
#include "shill/mock_control.h"
#include "shill/mock_event_dispatcher.h"
#include "shill/mock_manager.h"
#include "shill/mock_metrics.h"
#include "shill/store/key_value_store.h"
#include "shill/supplicant/mock_supplicant_interface_proxy.h"
#include "shill/supplicant/mock_supplicant_process_proxy.h"
#include "shill/test_event_dispatcher.h"
#include "shill/testing.h"
#include "shill/supplicant/wpa_supplicant.h"
#include "shill/wifi/hotspot_device.h"
#include "shill/wifi/hotspot_service.h"
#include "shill/wifi/local_device.h"
#include "shill/wifi/wifi_security.h"

using ::testing::_;
using ::testing::ByMove;
using ::testing::DoAll;
using ::testing::Eq;
using ::testing::Mock;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::StrictMock;
using ::testing::Test;

namespace shill {

namespace {
const char kDeviceName[] = "ap0";
const char kDeviceAddress[] = "00:01:02:03:04:05";
const char kHotspotSSID[] = "chromeOS-1234";
const char kHotspotPassphrase[] = "test0000";
const uint32_t kPhyIndex = 5678;
const RpcIdentifier kIfacePath = RpcIdentifier("/interface/path");
const RpcIdentifier kNetworkPath = RpcIdentifier("/network/path");
}  // namespace

class HotspotDeviceTest : public testing::Test {
 public:
  HotspotDeviceTest()
      : manager_(&control_interface_, &dispatcher_, &metrics_),
        device_(new HotspotDevice(
            &manager_, kDeviceName, kDeviceAddress, kPhyIndex, cb.Get())),
        supplicant_process_proxy_(new NiceMock<MockSupplicantProcessProxy>()),
        supplicant_interface_proxy_(
            new NiceMock<MockSupplicantInterfaceProxy>()) {
    manager_.supplicant_manager()->set_proxy(supplicant_process_proxy_);
    ON_CALL(*supplicant_process_proxy_, CreateInterface(_, _))
        .WillByDefault(DoAll(SetArgPointee<1>(kIfacePath), Return(true)));
    ON_CALL(control_interface_, CreateSupplicantInterfaceProxy(_, kIfacePath))
        .WillByDefault(Return(ByMove(std::move(supplicant_interface_proxy_))));
  }

  void DispatchPendingEvents() { dispatcher_.DispatchPendingEvents(); }

 protected:
  MockSupplicantInterfaceProxy* GetSupplicantInterfaceProxy() {
    return static_cast<MockSupplicantInterfaceProxy*>(
        device_->supplicant_interface_proxy_.get());
  }

  StrictMock<base::MockRepeatingCallback<void(LocalDevice::DeviceEvent,
                                              const LocalDevice*)>>
      cb;

  NiceMock<MockControl> control_interface_;
  EventDispatcherForTest dispatcher_;
  NiceMock<MockMetrics> metrics_;
  NiceMock<MockManager> manager_;

  scoped_refptr<HotspotDevice> device_;
  MockSupplicantProcessProxy* supplicant_process_proxy_;
  std::unique_ptr<MockSupplicantInterfaceProxy> supplicant_interface_proxy_;
};

TEST_F(HotspotDeviceTest, DeviceCleanStartStop) {
  EXPECT_CALL(*supplicant_process_proxy_, CreateInterface(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(kIfacePath), Return(true)));
  EXPECT_TRUE(device_->Start());

  EXPECT_CALL(*supplicant_process_proxy_, RemoveInterface(kIfacePath))
      .WillOnce(Return(true));
  // Expect no DeviceEvent::kInterfaceDisabled sent if the interface is
  // destroyed by caller not Kernel.
  EXPECT_CALL(cb, Run(_, _)).Times(0);
  EXPECT_TRUE(device_->Stop());
  DispatchPendingEvents();
  Mock::VerifyAndClearExpectations(&cb);
}

TEST_F(HotspotDeviceTest, DeviceExistStart) {
  EXPECT_CALL(*supplicant_process_proxy_, CreateInterface(_, _))
      .WillOnce(Return(false));
  EXPECT_CALL(*supplicant_process_proxy_, GetInterface(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(kIfacePath), Return(true)));
  EXPECT_TRUE(device_->Start());
}

TEST_F(HotspotDeviceTest, InterfaceDisabledEvent) {
  KeyValueStore props;
  props.Set<std::string>(WPASupplicant::kInterfacePropertyState,
                         WPASupplicant::kInterfaceStateInterfaceDisabled);

  // Expect supplicant_state_ change and kInterfaceDisabled DeviceEvent sent.
  EXPECT_CALL(cb, Run(LocalDevice::DeviceEvent::kInterfaceDisabled, _))
      .Times(1);
  device_->PropertiesChangedTask(props);
  EXPECT_EQ(device_->supplicant_state_,
            WPASupplicant::kInterfaceStateInterfaceDisabled);
  DispatchPendingEvents();
  Mock::VerifyAndClearExpectations(&cb);

  // Expect no supplicant_state_ change and no DeviceEvent sent with same state.
  EXPECT_CALL(cb, Run(_, _)).Times(0);
  device_->PropertiesChangedTask(props);
  EXPECT_EQ(device_->supplicant_state_,
            WPASupplicant::kInterfaceStateInterfaceDisabled);
  DispatchPendingEvents();
  Mock::VerifyAndClearExpectations(&cb);
}

TEST_F(HotspotDeviceTest, ConfigureDeconfigureService) {
  EXPECT_TRUE(device_->Start());

  // Configure service for the first time.
  auto service0 = std::make_unique<HotspotService>(
      device_, kHotspotSSID, kHotspotPassphrase,
      WiFiSecurity(WiFiSecurity::Mode::kWpa2));
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), AddNetwork(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(kNetworkPath), Return(true)));
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), SelectNetwork(Eq(kNetworkPath)))
      .WillOnce(Return(true));
  EXPECT_TRUE(device_->ConfigureService(std::move(service0)));

  // Configure a second service should be a no-op and return false.
  auto service1 = std::make_unique<HotspotService>(
      device_, kHotspotSSID, kHotspotPassphrase,
      WiFiSecurity(WiFiSecurity::Mode::kWpa2));
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), AddNetwork(_, _)).Times(0);
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), SelectNetwork(Eq(kNetworkPath)))
      .Times(0);
  EXPECT_FALSE(device_->ConfigureService(std::move(service1)));

  // Deconfigure service.
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), RemoveNetwork(Eq(kNetworkPath)))
      .WillOnce(Return(true));
  EXPECT_TRUE(device_->DeconfigureService());

  // Deconfigure service for the second time should be a no-op.
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), RemoveNetwork(Eq(kNetworkPath)))
      .Times(0);
  EXPECT_TRUE(device_->DeconfigureService());
}

TEST_F(HotspotDeviceTest, ServiceEvent) {
  auto service = std::make_unique<HotspotService>(
      device_, kHotspotSSID, kHotspotPassphrase,
      WiFiSecurity(WiFiSecurity::Mode::kWpa2));
  EXPECT_TRUE(device_->Start());
  ON_CALL(*GetSupplicantInterfaceProxy(), AddNetwork(_, _))
      .WillByDefault(DoAll(SetArgPointee<1>(kNetworkPath), Return(true)));
  EXPECT_TRUE(device_->ConfigureService(std::move(service)));

  KeyValueStore props;
  props.Set<std::string>(WPASupplicant::kInterfacePropertyState,
                         WPASupplicant::kInterfaceStateCompleted);

  // Expect supplicant_state_ change and kServiceUp DeviceEvent sent on
  // wpa_supplicant state kInterfaceStateCompleted.
  EXPECT_CALL(cb, Run(LocalDevice::DeviceEvent::kServiceUp, _)).Times(1);
  device_->PropertiesChangedTask(props);
  EXPECT_EQ(device_->supplicant_state_,
            WPASupplicant::kInterfaceStateCompleted);
  DispatchPendingEvents();
  Mock::VerifyAndClearExpectations(&cb);

  // Expect supplicant_state_ change and kServiceDown DeviceEvent sent on
  // wpa_supplicant state kInterfaceStateDisconnected.
  props.Set<std::string>(WPASupplicant::kInterfacePropertyState,
                         WPASupplicant::kInterfaceStateDisconnected);
  EXPECT_CALL(cb, Run(LocalDevice::DeviceEvent::kServiceDown, _)).Times(1);
  device_->PropertiesChangedTask(props);
  EXPECT_EQ(device_->supplicant_state_,
            WPASupplicant::kInterfaceStateDisconnected);
  DispatchPendingEvents();
  Mock::VerifyAndClearExpectations(&cb);

  // Expect supplicant_state_ change but no kServiceDown DeviceEvent sent on
  // further wpa_supplicant state change kInterfaceStateInactive.
  props.Set<std::string>(WPASupplicant::kInterfacePropertyState,
                         WPASupplicant::kInterfaceStateInactive);
  EXPECT_CALL(cb, Run(_, _)).Times(0);
  device_->PropertiesChangedTask(props);
  EXPECT_EQ(device_->supplicant_state_, WPASupplicant::kInterfaceStateInactive);
  DispatchPendingEvents();
  Mock::VerifyAndClearExpectations(&cb);
}

}  // namespace shill
