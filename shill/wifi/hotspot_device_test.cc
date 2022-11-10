// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/wifi/hotspot_device.h"

#include <string>

#include <base/memory/ref_counted.h>

#include "shill/error.h"
#include "shill/mock_control.h"
#include "shill/mock_event_dispatcher.h"
#include "shill/mock_manager.h"
#include "shill/mock_metrics.h"
#include "shill/supplicant/mock_supplicant_interface_proxy.h"
#include "shill/supplicant/mock_supplicant_process_proxy.h"
#include "shill/test_event_dispatcher.h"
#include "shill/testing.h"
#include "shill/wifi/hotspot_device.h"
#include "shill/wifi/mock_wifi_phy.h"
#include "shill/wifi/wifi_phy.h"

using ::testing::_;
using ::testing::DoAll;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SetArgPointee;
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
            &manager_,
            kDeviceName,
            kDeviceAddress,
            kPhyIndex,
            base::BindRepeating(&HotspotDeviceTest::OnLocalDeviceEvent,
                                base::Unretained(this)))),
        supplicant_process_proxy_(new NiceMock<MockSupplicantProcessProxy>()),
        supplicant_interface_proxy_(
            new NiceMock<MockSupplicantInterfaceProxy>()) {
    manager_.supplicant_manager()->set_proxy(supplicant_process_proxy_);
    ON_CALL(control_interface_, CreateSupplicantInterfaceProxy(_, _))
        .WillByDefault(
            Invoke(this, &HotspotDeviceTest::CreateSupplicantInterfaceProxy));
  }
  ~HotspotDeviceTest() override = default;

 private:
  std::unique_ptr<SupplicantInterfaceProxyInterface>
  CreateSupplicantInterfaceProxy(SupplicantEventDelegateInterface* delegate,
                                 const RpcIdentifier& object_path) {
    CHECK(supplicant_interface_proxy_);
    return std::move(supplicant_interface_proxy_);
  }

  void OnLocalDeviceEvent(LocalDevice::DeviceEvent event, LocalDevice* device) {
  }

  NiceMock<MockControl> control_interface_;
  EventDispatcherForTest dispatcher_;
  NiceMock<MockMetrics> metrics_;
  NiceMock<MockManager> manager_;

  // protected fields interspersed between private fields, due to
  // initialization order
 protected:
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
  EXPECT_TRUE(device_->Stop());
}

TEST_F(HotspotDeviceTest, DeviceExistStart) {
  EXPECT_CALL(*supplicant_process_proxy_, CreateInterface(_, _))
      .WillOnce(Return(false));
  EXPECT_CALL(*supplicant_process_proxy_, GetInterface(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(RpcIdentifier("/default/path")),
                      Return(true)));
  EXPECT_TRUE(device_->Start());
}

}  // namespace shill
