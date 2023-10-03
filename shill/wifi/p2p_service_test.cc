// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/wifi/p2p_service.h"

#include <memory>
#include <string>
#include <vector>

#include <base/test/mock_callback.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "shill/mock_control.h"
#include "shill/mock_manager.h"
#include "shill/mock_metrics.h"
#include "shill/test_event_dispatcher.h"
#include "shill/wifi/mock_local_device.h"

using ::testing::_;
using ::testing::NiceMock;
using ::testing::StrictMock;

namespace shill {

namespace {
constexpr char kHexSSID[] = "74657374";  // Hex encode for "test"
constexpr char kPassphrase[] = "passphrase";
constexpr uint32_t kFrequency = 2437;
}  // namespace

class P2PServiceTest : public testing::Test {
 public:
  P2PServiceTest() : manager_(&control_interface_, &dispatcher_, &metrics_) {}
  ~P2PServiceTest() override = default;

  std::unique_ptr<P2PService> CreateP2PService(LocalDeviceConstRefPtr device) {
    std::unique_ptr<P2PService> service =
        std::make_unique<P2PService>(device, kHexSSID, kPassphrase, kFrequency);
    return service;
  }

 private:
  StrictMock<base::MockRepeatingCallback<void(LocalDevice::DeviceEvent,
                                              const LocalDevice*)>>
      cb;

  NiceMock<MockControl> control_interface_;
  EventDispatcherForTest dispatcher_;
  NiceMock<MockMetrics> metrics_;
  NiceMock<MockManager> manager_;
};

}  // namespace shill
