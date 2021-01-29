// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/vpn/wireguard_driver.h"

#include <memory>

#include <gtest/gtest.h>

#include "shill/mock_control.h"
#include "shill/mock_device_info.h"
#include "shill/mock_manager.h"
#include "shill/mock_metrics.h"
#include "shill/mock_process_manager.h"
#include "shill/test_event_dispatcher.h"
#include "shill/vpn/mock_vpn_driver.h"

namespace shill {

using testing::_;
using testing::DoAll;
using testing::NiceMock;
using testing::Return;
using testing::SaveArg;

namespace {
constexpr pid_t kWireguardPid = 12345;
}  // namespace

class WireguardDriverTest : public testing::Test {
 public:
  WireguardDriverTest()
      : manager_(&control_, &dispatcher_, &metrics_),
        device_info_(&manager_),
        driver_(new WireguardDriver(&manager_, &process_manager_)) {
    manager_.set_mock_device_info(&device_info_);
  }

 protected:
  void InvokeConnectAsync() {
    driver_->ConnectAsync(&driver_event_handler_);
    EXPECT_CALL(
        process_manager_,
        StartProcessInMinijail(_, _, _, _, "vpn", "vpn",
                               CAP_TO_MASK(CAP_NET_ADMIN), true, true, _))
        .WillOnce(DoAll(SaveArg<9>(&wireguard_exit_callback_),
                        Return(kWireguardPid)));
    dispatcher_.DispatchPendingEvents();
  }

  MockControl control_;
  EventDispatcherForTest dispatcher_;
  MockMetrics metrics_;
  MockProcessManager process_manager_;
  MockManager manager_;
  NiceMock<MockDeviceInfo> device_info_;
  MockVPNDriverEventHandler driver_event_handler_;
  std::unique_ptr<WireguardDriver> driver_;

  base::RepeatingCallback<void(int)> wireguard_exit_callback_;
};

// TODO(b/177876632): More tests for the connect flow and config file.

TEST_F(WireguardDriverTest, Disconnect) {
  InvokeConnectAsync();
  EXPECT_CALL(process_manager_, StopProcess(kWireguardPid));
  driver_->Disconnect();
}

TEST_F(WireguardDriverTest, SpawnWireguardProcessFailed) {
  driver_->ConnectAsync(&driver_event_handler_);
  EXPECT_CALL(process_manager_,
              StartProcessInMinijail(_, _, _, _, _, _, _, _, _, _))
      .WillOnce(Return(-1));
  EXPECT_CALL(driver_event_handler_, OnDriverFailure(_, _));
  dispatcher_.DispatchPendingEvents();
}

TEST_F(WireguardDriverTest, WireguardProcessExitedUnexpectedly) {
  InvokeConnectAsync();
  EXPECT_CALL(driver_event_handler_, OnDriverFailure(_, _));
  wireguard_exit_callback_.Run(1);
}

TEST_F(WireguardDriverTest, OnConnectTimeout) {
  InvokeConnectAsync();
  EXPECT_CALL(driver_event_handler_, OnDriverFailure(_, _));
  driver_->OnConnectTimeout();
}

}  // namespace shill
