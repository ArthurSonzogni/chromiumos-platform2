// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/wifi/hotspot_device.h"

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
#include "shill/wifi/local_device.h"

using ::testing::_;
using ::testing::DoAll;
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
const uint32_t kPhyIndex = 5678;
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
    ON_CALL(control_interface_, CreateSupplicantInterfaceProxy(_, _))
        .WillByDefault(
            Invoke(this, &HotspotDeviceTest::CreateSupplicantInterfaceProxy));
  }
  ~HotspotDeviceTest() override = default;

  void DispatchPendingEvents() { dispatcher_.DispatchPendingEvents(); }

 private:
  std::unique_ptr<SupplicantInterfaceProxyInterface>
  CreateSupplicantInterfaceProxy(SupplicantEventDelegateInterface* delegate,
                                 const RpcIdentifier& object_path) {
    CHECK(supplicant_interface_proxy_);
    return std::move(supplicant_interface_proxy_);
  }

  NiceMock<MockControl> control_interface_;
  EventDispatcherForTest dispatcher_;
  NiceMock<MockMetrics> metrics_;
  NiceMock<MockManager> manager_;

  // protected fields interspersed between private fields, due to
  // initialization order
 protected:
  StrictMock<
      base::MockRepeatingCallback<void(LocalDevice::DeviceEvent, LocalDevice*)>>
      cb;
  scoped_refptr<HotspotDevice> device_;
  MockSupplicantProcessProxy* supplicant_process_proxy_;

 private:
  std::unique_ptr<MockSupplicantInterfaceProxy> supplicant_interface_proxy_;
};

TEST_F(HotspotDeviceTest, DeviceCleanStartStop) {
  EXPECT_CALL(*supplicant_process_proxy_, CreateInterface(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(RpcIdentifier("/default/path")),
                      Return(true)));
  EXPECT_TRUE(device_->Start());

  EXPECT_CALL(*supplicant_process_proxy_, RemoveInterface(_))
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
      .WillOnce(DoAll(SetArgPointee<1>(RpcIdentifier("/default/path")),
                      Return(true)));
  EXPECT_TRUE(device_->Start());
}

TEST_F(HotspotDeviceTest, InterfaceDisabled) {
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

}  // namespace shill
