// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/network.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "shill/mock_control.h"
#include "shill/mock_device_info.h"
#include "shill/mock_manager.h"
#include "shill/network/dhcp_controller.h"
#include "shill/network/mock_dhcp_controller.h"
#include "shill/network/mock_dhcp_provider.h"
#include "shill/network/mock_network.h"
#include "shill/technology.h"
#include "shill/test_event_dispatcher.h"

namespace shill {
namespace {

using ::testing::_;
using ::testing::InvokeWithoutArgs;
using ::testing::Mock;
using ::testing::NiceMock;
using ::testing::Return;

constexpr int kTestIfindex = 123;
constexpr char kTestIfname[] = "eth_test";
constexpr auto kTestTechnology = Technology::kUnknown;

// Allows us to fake/mock some functions in this test.
class NetworkInTest : public Network {
 public:
  NetworkInTest(int interface_index,
                const std::string& interface_name,
                Technology technology,
                bool fixed_ip_params,
                EventHandler* event_handler,
                ControlInterface* control_interface,
                DeviceInfo* device_info,
                EventDispatcher* dispatcher)
      : Network(interface_index,
                interface_name,
                technology,
                fixed_ip_params,
                event_handler,
                control_interface,
                device_info,
                dispatcher) {
    ON_CALL(*this, SetIPFlag(_, _, _)).WillByDefault(Return(true));
  }

  MOCK_METHOD(bool,
              SetIPFlag,
              (IPAddress::Family, const std::string&, const std::string&),
              (override));
};

class NetworkTest : public ::testing::Test {
 public:
  NetworkTest()
      : manager_(&control_interface_, &dispatcher_, nullptr),
        device_info_(&manager_) {
    network_ = std::make_unique<NetworkInTest>(
        kTestIfindex, kTestIfname, kTestTechnology,
        /*fixed_ip_params=*/false, &event_handler_, &control_interface_,
        &device_info_, &dispatcher_);
    network_->set_dhcp_provider_for_testing(&dhcp_provider_);
  }
  ~NetworkTest() override { network_ = nullptr; }

  // Expects calling CreateController() on DHCPProvider, and the following
  // RequestIP() call will return |request_ip_result|. The pointer to the
  // returned DHCPController will be stored in |dhcp_controller_|.
  void ExpectCreateDHCPController(bool request_ip_result) {
    EXPECT_CALL(dhcp_provider_, CreateController(_, _, _))
        .WillOnce(InvokeWithoutArgs([this]() {
          auto controller = std::make_unique<MockDHCPController>(
              &control_interface_, kTestIfname);
          EXPECT_CALL(*controller, RequestIP()).WillOnce(Return(true));
          dhcp_controller_ = controller.get();
          return controller;
        }));
  }

 protected:
  // Order does matter in this group. See the constructor.
  NiceMock<MockControl> control_interface_;
  EventDispatcherForTest dispatcher_;
  MockManager manager_;
  NiceMock<MockDeviceInfo> device_info_;

  MockDHCPProvider dhcp_provider_;
  MockNetworkEventHandler event_handler_;

  std::unique_ptr<NetworkInTest> network_;

  // Owned by |network_|. Not guaranteed valid even if it's not null.
  MockDHCPController* dhcp_controller_ = nullptr;
};

TEST_F(NetworkTest, OnNetworkStoppedCalledOnStopAfterStart) {
  EXPECT_CALL(event_handler_, OnNetworkStopped(_)).Times(0);
  ExpectCreateDHCPController(true);
  network_->Start(Network::StartOptions{.dhcp = DHCPProvider::Options{}});

  EXPECT_CALL(event_handler_, OnNetworkStopped(false)).Times(1);
  network_->Stop();
  Mock::VerifyAndClearExpectations(&event_handler_);

  // Additional Stop() should not trigger the callback.
  EXPECT_CALL(event_handler_, OnNetworkStopped(_)).Times(0);
  network_->Stop();
  Mock::VerifyAndClearExpectations(&event_handler_);
}

TEST_F(NetworkTest, OnNetworkStoppedNoCalledOnStopWithoutStart) {
  EXPECT_CALL(event_handler_, OnNetworkStopped(_)).Times(0);
  network_->Stop();
}

TEST_F(NetworkTest, OnNetworkStoppedNoCalledOnStart) {
  EXPECT_CALL(event_handler_, OnNetworkStopped(_)).Times(0);
  ExpectCreateDHCPController(true);
  network_->Start(Network::StartOptions{.dhcp = DHCPProvider::Options{}});

  ExpectCreateDHCPController(true);
  network_->Start(Network::StartOptions{.dhcp = DHCPProvider::Options{}});
}

TEST_F(NetworkTest, OnNetworkStoppedCalledOnDHCPFailure) {
  ExpectCreateDHCPController(true);
  network_->Start(Network::StartOptions{.dhcp = DHCPProvider::Options{}});

  EXPECT_CALL(event_handler_, OnNetworkStopped(true)).Times(1);
  ASSERT_NE(dhcp_controller_, nullptr);
  dhcp_controller_->TriggerFailureCallback();
}

TEST_F(NetworkTest, EnableARPFilteringOnStart) {
  ExpectCreateDHCPController(true);
  EXPECT_CALL(*network_, SetIPFlag(IPAddress::kFamilyIPv4, "arp_announce", "2"))
      .WillOnce(Return(true));
  EXPECT_CALL(*network_, SetIPFlag(IPAddress::kFamilyIPv4, "arp_ignore", "1"))
      .WillOnce(Return(true));
  network_->Start(Network::StartOptions{.dhcp = DHCPProvider::Options{}});
}

}  // namespace
}  // namespace shill
