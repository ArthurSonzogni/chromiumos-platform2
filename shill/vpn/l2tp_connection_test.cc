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

  void InvokeGetLogin(std::string* user, std::string* password) {
    GetLogin(user, password);
  }

  void InvokeNotify(const std::string& reason,
                    const std::map<std::string, std::string> dict) {
    Notify(reason, dict);
  }

  void set_config(std::unique_ptr<Config> config) {
    config_ = std::move(config);
  }

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

TEST_F(L2TPConnectionTest, PPPGetLogin) {
  constexpr char kUser[] = "user";
  constexpr char kPassword[] = "password";
  auto config = std::make_unique<L2TPConnection::Config>();
  config->user = kUser;
  config->password = kPassword;

  l2tp_connection_->set_config(std::move(config));

  std::string actual_user;
  std::string actual_password;
  l2tp_connection_->InvokeGetLogin(&actual_user, &actual_password);
  EXPECT_EQ(actual_user, kUser);
  EXPECT_EQ(actual_password, kPassword);
}

TEST_F(L2TPConnectionTest, PPPNotifyConnected) {
  l2tp_connection_->set_state(VPNConnection::State::kConnecting);

  constexpr char kIfName[] = "ppp0";
  constexpr int kIfIndex = 321;
  constexpr char kLocalIPAddress[] = "10.0.0.2";
  std::map<std::string, std::string> config{
      {kPPPInterfaceName, kIfName}, {kPPPInternalIP4Address, kLocalIPAddress}};

  // No callbacks should be invoked when authenticating.
  EXPECT_CALL(callbacks_, OnConnected(_, _, _)).Times(0);
  EXPECT_CALL(callbacks_, OnFailure(_)).Times(0);
  EXPECT_CALL(callbacks_, OnStopped()).Times(0);
  l2tp_connection_->InvokeNotify(kPPPReasonAuthenticating, config);
  l2tp_connection_->InvokeNotify(kPPPReasonAuthenticated, config);
  dispatcher_.task_environment().RunUntilIdle();

  // Expects OnConnected() when kPPPReasonConnect event comes.
  IPConfig::Properties actual_ip_properties;
  EXPECT_CALL(callbacks_, OnConnected(kIfName, kIfIndex, _))
      .WillOnce(SaveArg<2>(&actual_ip_properties));
  EXPECT_CALL(device_info_, GetIndex(kIfName)).WillOnce(Return(kIfIndex));
  l2tp_connection_->InvokeNotify(kPPPReasonConnect, config);
  dispatcher_.task_environment().RunUntilIdle();

  EXPECT_EQ(actual_ip_properties.address, kLocalIPAddress);
}

TEST_F(L2TPConnectionTest, PPPNotifyConnectedWithoutDeviceInfoReady) {
  l2tp_connection_->set_state(VPNConnection::State::kConnecting);

  constexpr char kIfName[] = "ppp0";
  constexpr int kIfIndex = 321;
  std::map<std::string, std::string> config{{kPPPInterfaceName, kIfName}};

  // The object should register the callback with DeviceInfo if the interface is
  // not known by shill now.
  DeviceInfo::LinkReadyCallback link_ready_cb;
  EXPECT_CALL(callbacks_, OnConnected(kIfName, kIfIndex, _)).Times(0);
  EXPECT_CALL(device_info_, GetIndex(kIfName)).WillOnce(Return(-1));
  EXPECT_CALL(device_info_, AddVirtualInterfaceReadyCallback(kIfName, _))
      .WillOnce([&](const std::string&, DeviceInfo::LinkReadyCallback cb) {
        link_ready_cb = std::move(cb);
      });
  l2tp_connection_->InvokeNotify(kPPPReasonConnect, config);
  dispatcher_.task_environment().RunUntilIdle();

  // Expects OnConnected() when the link is ready.
  std::move(link_ready_cb).Run(kIfName, kIfIndex);
  EXPECT_CALL(callbacks_, OnConnected(kIfName, kIfIndex, _));
  dispatcher_.task_environment().RunUntilIdle();
}

TEST_F(L2TPConnectionTest, PPPNotifyDisconnect) {
  l2tp_connection_->set_state(VPNConnection::State::kConnected);
  std::map<std::string, std::string> dict;
  EXPECT_CALL(callbacks_, OnFailure(_));
  l2tp_connection_->InvokeNotify(kPPPReasonDisconnect, dict);
  dispatcher_.task_environment().RunUntilIdle();

  // The signal shouldn't be sent out twice if the event comes again.
  l2tp_connection_->InvokeNotify(kPPPReasonDisconnect, dict);
  dispatcher_.task_environment().RunUntilIdle();
}

}  // namespace
}  // namespace shill
