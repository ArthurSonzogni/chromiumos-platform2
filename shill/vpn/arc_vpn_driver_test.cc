// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/vpn/arc_vpn_driver.h"

#include <memory>
#include <string>

#include <base/functional/bind.h>
#include <base/memory/ptr_util.h>
#include <gtest/gtest.h>

#include "shill/mock_control.h"
#include "shill/mock_manager.h"
#include "shill/mock_metrics.h"
#include "shill/mock_virtual_device.h"
#include "shill/store/fake_store.h"
#include "shill/test_event_dispatcher.h"
#include "shill/vpn/mock_vpn_driver.h"
#include "shill/vpn/mock_vpn_provider.h"
#include "shill/vpn/vpn_provider.h"
#include "shill/vpn/vpn_types.h"

using testing::_;
using testing::NiceMock;
using testing::Return;

namespace shill {

namespace {

constexpr char kInterfaceName[] = "arcbr0";
constexpr int kInterfaceIndex = 123;
constexpr char kStorageId[] = "fakestorage";

}  // namespace

class ArcVpnDriverTest : public testing::Test {
 public:
  ArcVpnDriverTest()
      : manager_(&control_, &dispatcher_, &metrics_),
        device_(new MockVirtualDevice(
            &manager_, kInterfaceName, kInterfaceIndex, Technology::kVPN)),
        driver_(new ArcVpnDriver(&manager_, nullptr)) {}

  ~ArcVpnDriverTest() override = default;

  void SetUp() override {
    manager_.vpn_provider_ = std::make_unique<MockVPNProvider>();
    manager_.vpn_provider_->manager_ = &manager_;
    manager_.UpdateProviderMapping();
  }

  void TearDown() override {
    manager_.vpn_provider_.reset();
    device_ = nullptr;
  }

  void LoadPropertiesFromStore() {
    const std::string kProviderHostValue = "arcvpn";
    const std::string kProviderTypeValue = "arcvpn";

    store_.SetString(kStorageId, kProviderHostProperty, kProviderHostValue);
    store_.SetString(kStorageId, kProviderTypeProperty, kProviderTypeValue);
    driver_->Load(&store_, kStorageId);
  }

  MockDeviceInfo* device_info() { return manager_.mock_device_info(); }

 protected:
  MockControl control_;
  EventDispatcherForTest dispatcher_;
  MockMetrics metrics_;
  MockManager manager_;
  scoped_refptr<MockVirtualDevice> device_;
  FakeStore store_;
  MockVPNDriverEventHandler event_handler_;
  std::unique_ptr<ArcVpnDriver> driver_;
};

TEST_F(ArcVpnDriverTest, VPNType) {
  EXPECT_EQ(driver_->vpn_type(), VPNType::kARC);
}

TEST_F(ArcVpnDriverTest, ConnectAsync) {
  LoadPropertiesFromStore();
  EXPECT_CALL(*device_info(), GetIndex(_)).WillOnce(Return(kInterfaceIndex));
  EXPECT_CALL(event_handler_,
              OnDriverConnected(std::string(VPNProvider::kArcBridgeIfName),
                                kInterfaceIndex))
      .Times(1);
  driver_->ConnectAsync(&event_handler_);
  dispatcher_.task_environment().RunUntilIdle();
}

TEST_F(ArcVpnDriverTest, GetNetworkConfig) {
  const auto network_config = driver_->GetNetworkConfig();
  ASSERT_NE(network_config, nullptr);
  EXPECT_TRUE(network_config->ipv6_blackhole_route);
}

}  // namespace shill
