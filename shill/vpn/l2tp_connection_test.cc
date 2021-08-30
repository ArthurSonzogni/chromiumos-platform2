// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/vpn/l2tp_connection.h"

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/run_loop.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "shill/mock_control.h"
#include "shill/mock_device_info.h"
#include "shill/mock_manager.h"
#include "shill/mock_metrics.h"
#include "shill/mock_process_manager.h"
#include "shill/ppp_device.h"
#include "shill/rpc_task.h"
#include "shill/test_event_dispatcher.h"
#include "shill/vpn/fake_vpn_util.h"
#include "shill/vpn/vpn_connection_under_test.h"

namespace shill {

class L2TPConnectionUnderTest : public L2TPConnection {
 public:
  L2TPConnectionUnderTest(std::unique_ptr<Config> config,
                          std::unique_ptr<Callbacks> callbacks,
                          ControlInterface* control_interface,
                          DeviceInfo* device_info,
                          EventDispatcher* dispatcher,
                          ProcessManager* process_manager)
      : L2TPConnection(std::move(config),
                       std::move(callbacks),
                       control_interface,
                       device_info,
                       dispatcher,
                       process_manager) {
    vpn_util_ = std::make_unique<FakeVPNUtil>();
  }

  base::FilePath SetTempDir() {
    CHECK(temp_dir_.CreateUniqueTempDir());
    return temp_dir_.GetPath();
  }

  void InvokeStartXl2tpd() { StartXl2tpd(); }

  void set_state(State state) { state_ = state; }
};

namespace {

using testing::_;
using testing::DoAll;
using testing::Return;
using testing::SaveArg;

class MockCallbacks {
 public:
  MOCK_METHOD(void,
              OnConnected,
              (const std::string& link_name,
               int interface_index,
               const IPConfig::Properties& ip_properties));
  MOCK_METHOD(void, OnFailure, (Service::ConnectFailure));
  MOCK_METHOD(void, OnStopped, ());
};

class L2TPConnectionTest : public testing::Test {
 public:
  L2TPConnectionTest()
      : manager_(&control_, &dispatcher_, &metrics_), device_info_(&manager_) {
    auto callbacks = std::make_unique<VPNConnection::Callbacks>(
        base::BindRepeating(&MockCallbacks::OnConnected,
                            base::Unretained(&callbacks_)),
        base::BindOnce(&MockCallbacks::OnFailure,
                       base::Unretained(&callbacks_)),
        base::BindOnce(&MockCallbacks::OnStopped,
                       base::Unretained(&callbacks_)));

    l2tp_connection_ = std::make_unique<L2TPConnectionUnderTest>(
        std::make_unique<L2TPConnection::Config>(), std::move(callbacks),
        &control_, &device_info_, &dispatcher_, &process_manager_);
  }

 protected:
  MockControl control_;
  EventDispatcherForTest dispatcher_;
  MockMetrics metrics_;
  MockManager manager_;
  MockDeviceInfo device_info_;
  MockProcessManager process_manager_;

  MockCallbacks callbacks_;
  std::unique_ptr<L2TPConnectionUnderTest> l2tp_connection_;
};

TEST_F(L2TPConnectionTest, StartXl2tpd) {
  l2tp_connection_->SetTempDir();

  std::map<std::string, std::string> actual_env;
  const base::FilePath kExpectedProgramPath("/usr/sbin/xl2tpd");
  constexpr uint64_t kExpectedCapMask = CAP_TO_MASK(CAP_NET_ADMIN);
  EXPECT_CALL(process_manager_,
              StartProcessInMinijail(_, kExpectedProgramPath, _, _, "vpn",
                                     "vpn", kExpectedCapMask, true, true, _))
      .WillOnce(DoAll(SaveArg<3>(&actual_env), Return(123)));

  l2tp_connection_->InvokeStartXl2tpd();

  // Environment should contains variables needed by pppd.
  EXPECT_NE(actual_env.find(kRpcTaskServiceVariable), actual_env.end());
  EXPECT_NE(actual_env.find(kRpcTaskPathVariable), actual_env.end());
  EXPECT_NE(actual_env.find("LNS_ADDRESS"), actual_env.end());
}

TEST_F(L2TPConnectionTest, Xl2tpdExitedUnexpectedly) {
  l2tp_connection_->SetTempDir();
  l2tp_connection_->set_state(VPNConnection::State::kConnecting);

  base::OnceCallback<void(int)> exit_cb;
  EXPECT_CALL(process_manager_, StartProcessInMinijail(_, _, _, _, "vpn", "vpn",
                                                       _, true, true, _))
      .WillOnce(DoAll(SaveArg<9>(&exit_cb), Return(123)));

  l2tp_connection_->InvokeStartXl2tpd();

  std::move(exit_cb).Run(1);

  EXPECT_CALL(callbacks_, OnFailure(_));
  dispatcher_.task_environment().RunUntilIdle();
}

}  // namespace
}  // namespace shill
