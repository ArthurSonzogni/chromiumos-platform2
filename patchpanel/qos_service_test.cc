// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/qos_service.h"

#include <memory>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "patchpanel/mock_datapath.h"
#include "patchpanel/shill_client.h"

namespace patchpanel {
namespace {

using ::testing::Mock;
using ::testing::StrictMock;

// Verifies the interactions between QoSService and Datapath when feature on the
// events of feature enable/disable and device change events.
TEST(QoSServiceTest, EnableDisableQoSFeature) {
  using Device = ShillClient::Device;
  const Device kEth0 = {
      .type = Device::Type::kEthernet,
      .ifname = "eth0",
  };
  const Device kEth1 = {
      .type = Device::Type::kEthernet,
      .ifname = "eth1",
  };
  const Device kWlan0 = {
      .type = Device::Type::kWifi,
      .ifname = "wlan0",
  };
  const Device kWlan1 = {
      .type = Device::Type::kWifi,
      .ifname = "wlan1",
  };

  StrictMock<MockDatapath> datapath;
  QoSService qos_svc(&datapath);

  // No interaction with Datapath before feature is enabled.
  qos_svc.OnPhysicalDeviceAdded(kEth0);
  qos_svc.OnPhysicalDeviceAdded(kWlan0);
  qos_svc.OnPhysicalDeviceRemoved(kWlan0);
  qos_svc.OnPhysicalDeviceAdded(kWlan0);
  Mock::VerifyAndClearExpectations(&datapath);

  // On feature enabled, the detection chain should be enabled, and the DSCP
  // marking chain for the existing interface should be enabled.
  EXPECT_CALL(datapath, EnableQoSDetection);
  EXPECT_CALL(datapath, EnableQoSApplyingDSCP("wlan0"));
  qos_svc.Enable();
  Mock::VerifyAndClearExpectations(&datapath);

  // No interaction with Datapath on uninteresting or already-tracked
  // interfaces.
  qos_svc.OnPhysicalDeviceAdded(kEth1);
  qos_svc.OnPhysicalDeviceAdded(kWlan0);
  Mock::VerifyAndClearExpectations(&datapath);

  // Device change events on interesting interfaces.
  EXPECT_CALL(datapath, DisableQoSApplyingDSCP("wlan0"));
  EXPECT_CALL(datapath, EnableQoSApplyingDSCP("wlan1"));
  qos_svc.OnPhysicalDeviceRemoved(kWlan0);
  qos_svc.OnPhysicalDeviceAdded(kWlan1);
  Mock::VerifyAndClearExpectations(&datapath);

  // On feature disabled.
  EXPECT_CALL(datapath, DisableQoSDetection);
  EXPECT_CALL(datapath, DisableQoSApplyingDSCP("wlan1"));
  qos_svc.Disable();
  Mock::VerifyAndClearExpectations(&datapath);

  // Device change events when disabled, and then enable again.
  qos_svc.OnPhysicalDeviceRemoved(kWlan1);
  qos_svc.OnPhysicalDeviceAdded(kWlan0);
  EXPECT_CALL(datapath, EnableQoSDetection);
  EXPECT_CALL(datapath, EnableQoSApplyingDSCP("wlan0"));
  qos_svc.Enable();
  Mock::VerifyAndClearExpectations(&datapath);
}

}  // namespace
}  // namespace patchpanel
