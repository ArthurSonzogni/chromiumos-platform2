// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/wifi/p2p_device.h"

#include <string>

#include <base/test/mock_callback.h>

#include "shill/mock_control.h"
#include "shill/mock_event_dispatcher.h"
#include "shill/mock_manager.h"
#include "shill/mock_metrics.h"
#include "shill/test_event_dispatcher.h"
#include "shill/wifi/local_device.h"
#include "shill/wifi/mock_wifi_phy.h"
#include "shill/wifi/mock_wifi_provider.h"
#include "shill/wifi/wifi_security.h"

using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::StrictMock;
using ::testing::Test;

namespace shill {

namespace {
const char kPrimaryInterfaceName[] = "wlan0";
const uint32_t kPhyIndex = 5678;
}  // namespace

class P2PDeviceTest : public testing::Test {
 public:
  P2PDeviceTest()
      : manager_(&control_interface_, &dispatcher_, &metrics_),
        wifi_provider_(new NiceMock<MockWiFiProvider>(&manager_)),
        wifi_phy_(kPhyIndex) {
    // Replace the Manager's WiFi provider with a mock.
    manager_.wifi_provider_.reset(wifi_provider_);
    // Update the Manager's map from technology to provider.
    manager_.UpdateProviderMapping();
    ON_CALL(*wifi_provider_, GetPhyAtIndex(kPhyIndex))
        .WillByDefault(Return(&wifi_phy_));
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
};

TEST_F(P2PDeviceTest, DeviceOnOff) {
  scoped_refptr<P2PDevice> device =
      new P2PDevice(&manager_, LocalDevice::IfaceType::kP2PGO,
                    kPrimaryInterfaceName, kPhyIndex, cb.Get());
  device->Start();
  device->Stop();
}

}  // namespace shill
