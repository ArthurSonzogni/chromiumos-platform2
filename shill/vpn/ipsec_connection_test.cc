// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/vpn/ipsec_connection.h"

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

#include "shill/mock_process_manager.h"
#include "shill/test_event_dispatcher.h"
#include "shill/vpn/fake_vpn_util.h"
#include "shill/vpn/vpn_connection_under_test.h"

namespace shill {

class IPsecConnectionUnderTest : public IPsecConnection {
 public:
  IPsecConnectionUnderTest(std::unique_ptr<Config> config,
                           std::unique_ptr<Callbacks> callbacks,
                           std::unique_ptr<VPNConnection> l2tp_connection,
                           EventDispatcher* dispatcher,
                           ProcessManager* process_manager)
      : IPsecConnection(std::move(config),
                        std::move(callbacks),
                        std::move(l2tp_connection),
                        dispatcher,
                        process_manager) {
    vpn_util_ = std::make_unique<FakeVPNUtil>();
  }

  IPsecConnectionUnderTest(const IPsecConnectionUnderTest&) = delete;
  IPsecConnectionUnderTest& operator=(const IPsecConnectionUnderTest&) = delete;

  base::FilePath SetTempDir() {
    CHECK(temp_dir_.CreateUniqueTempDir());
    return temp_dir_.GetPath();
  }

  void InvokeScheduleConnectTask(ConnectStep step) {
    IPsecConnection::ScheduleConnectTask(step);
  }

  void set_config(std::unique_ptr<Config> config) {
    config_ = std::move(config);
  }

  void set_strongswan_conf_path(const base::FilePath& path) {
    strongswan_conf_path_ = path;
  }

  void set_swanctl_conf_path(const base::FilePath& path) {
    swanctl_conf_path_ = path;
  }

  void set_charon_pid(pid_t pid) { charon_pid_ = pid; }

  void set_vici_socket_path(const base::FilePath& path) {
    vici_socket_path_ = path;
  }

  void set_state(State state) { state_ = state; }

  MOCK_METHOD(void, ScheduleConnectTask, (ConnectStep), (override));
};

namespace {

using ConnectStep = IPsecConnection::ConnectStep;

using testing::_;
using testing::AllOf;
using testing::DoAll;
using testing::Return;
using testing::WithArg;

// Note that there is a MACRO in this string so we cannot use raw string literal
// here.
constexpr char kExpectedStrongSwanConf[] =
    "charon {\n"
    "  accept_unencrypted_mainmode_messages = yes\n"
    "  ignore_routing_tables = 0\n"
    "  install_routes = no\n"
    "  routing_table = 0\n"
    "  syslog {\n"
    "    daemon {\n"
    "      ike = 2\n"
    "      cfg = 2\n"
    "      knl = 2\n"
    "    }\n"
    "  }\n"
    "  plugins {\n"
    "    pkcs11 {\n"
    "      modules {\n"
    "        crypto_module {\n"
    "          path = " PKCS11_LIB
    "\n"
    "        }\n"
    "      }\n"
    "    }\n"
    "  }\n"
    "}";

// Expected contents of swanctl.conf in WriteSwanctlConfig test.
constexpr char kExpectedSwanctlConfWithPSK[] = R"(connections {
  vpn {
    local_addrs = "0.0.0.0/0,::/0"
    proposals = "aes128-sha256-modp3072,aes128-sha1-modp2048,3des-sha1-modp1536,3des-sha1-modp1024,default"
    remote_addrs = "10.0.0.1"
    version = "1"
    local-1 {
      auth = "psk"
    }
    remote-1 {
      auth = "psk"
    }
    local-2 {
      auth = "xauth"
      xauth_id = "xauth_user"
    }
    children {
      managed {
        esp_proposals = "aes128gcm16,aes128-sha256,aes128-sha1,3des-sha1,3des-md5,default"
        local_ts = "dynamic[17/1701]"
        mode = "transport"
        remote_ts = "dynamic[17/1701]"
      }
    }
  }
}
secrets {
  ike-1 {
    secret = "this is psk"
  }
  xauth-1 {
    id = "xauth_user"
    secret = "xauth_password"
  }
})";

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

class IPsecConnectionTest : public testing::Test {
 public:
  IPsecConnectionTest() {
    auto callbacks = std::make_unique<VPNConnection::Callbacks>(
        base::BindRepeating(&MockCallbacks::OnConnected,
                            base::Unretained(&callbacks_)),
        base::BindOnce(&MockCallbacks::OnFailure,
                       base::Unretained(&callbacks_)),
        base::BindOnce(&MockCallbacks::OnStopped,
                       base::Unretained(&callbacks_)));
    auto l2tp_tmp =
        std::make_unique<VPNConnectionUnderTest>(nullptr, &dispatcher_);
    l2tp_connection_ = l2tp_tmp.get();
    ipsec_connection_ = std::make_unique<IPsecConnectionUnderTest>(
        std::make_unique<IPsecConnection::Config>(), std::move(callbacks),
        std::move(l2tp_tmp), &dispatcher_, &process_manager_);
  }

 protected:
  EventDispatcherForTest dispatcher_;
  MockCallbacks callbacks_;
  MockProcessManager process_manager_;

  std::unique_ptr<IPsecConnectionUnderTest> ipsec_connection_;
  VPNConnectionUnderTest* l2tp_connection_;  // owned by ipsec_connection_;
};

TEST_F(IPsecConnectionTest, WriteStrongSwanConfig) {
  base::FilePath temp_dir = ipsec_connection_->SetTempDir();

  // Signal should be send out at the end of the execution.
  EXPECT_CALL(*ipsec_connection_,
              ScheduleConnectTask(ConnectStep::kStrongSwanConfigWritten));

  ipsec_connection_->InvokeScheduleConnectTask(ConnectStep::kStart);

  // IPsecConnection should write the config to the `strongswan.conf` file under
  // the temp dir it created.
  base::FilePath expected_path = temp_dir.Append("strongswan.conf");
  ASSERT_TRUE(base::PathExists(expected_path));
  std::string actual_content;
  ASSERT_TRUE(base::ReadFileToString(expected_path, &actual_content));
  EXPECT_EQ(actual_content, kExpectedStrongSwanConf);

  // The file should be deleted after destroying the IPsecConnection object.
  ipsec_connection_ = nullptr;
  ASSERT_FALSE(base::PathExists(expected_path));
}

TEST_F(IPsecConnectionTest, StartCharon) {
  ipsec_connection_->set_state(VPNConnection::State::kConnecting);

  const base::FilePath kStrongSwanConfPath("/tmp/strongswan.conf");
  ipsec_connection_->set_strongswan_conf_path(kStrongSwanConfPath);

  // Prepares the file path under the scoped temp dir. The actual file will be
  // created later to simulate the case that it is created by the charon
  // process.
  const auto tmp_dir = ipsec_connection_->SetTempDir();
  const base::FilePath kViciSocketPath = tmp_dir.Append("charon.vici");
  ipsec_connection_->set_vici_socket_path(kViciSocketPath);

  // Expects call for starting charon process.
  const base::FilePath kExpectedProgramPath("/usr/libexec/ipsec/charon");
  const std::vector<std::string> kExpectedArgs = {};
  const std::map<std::string, std::string> kExpectedEnv = {
      {"STRONGSWAN_CONF", kStrongSwanConfPath.value()}};
  constexpr uint64_t kExpectedCapMask =
      CAP_TO_MASK(CAP_NET_ADMIN) | CAP_TO_MASK(CAP_NET_BIND_SERVICE) |
      CAP_TO_MASK(CAP_NET_RAW) | CAP_TO_MASK(CAP_SETGID);
  EXPECT_CALL(process_manager_,
              StartProcessInMinijail(
                  _, kExpectedProgramPath, kExpectedArgs, kExpectedEnv,
                  AllOf(MinijailOptionsMatchUserGroup("vpn", "vpn"),
                        MinijailOptionsMatchCapMask(kExpectedCapMask),
                        MinijailOptionsMatchInheritSupplumentaryGroup(true),
                        MinijailOptionsMatchCloseNonstdFDs(true)),
                  _))
      .WillOnce(Return(123));

  // Triggers the task.
  ipsec_connection_->InvokeScheduleConnectTask(
      ConnectStep::kStrongSwanConfigWritten);

  // Creates the socket file, and then IPsecConnection should be notified and
  // forward the step. We use a RunLoop here instead of RunUtilIdle() since it
  // cannot be guaranteed that FilePathWatcher posted the task before
  // RunUtilIdle() is called.
  CHECK(base::WriteFile(kViciSocketPath, ""));
  base::RunLoop run_loop;
  EXPECT_CALL(*ipsec_connection_,
              ScheduleConnectTask(ConnectStep::kCharonStarted))
      .WillOnce([&](ConnectStep) { run_loop.Quit(); });
  run_loop.Run();
}

TEST_F(IPsecConnectionTest, StartCharonFailWithStartProcess) {
  ipsec_connection_->set_state(VPNConnection::State::kConnecting);

  EXPECT_CALL(process_manager_, StartProcessInMinijail(_, _, _, _, _, _))
      .WillOnce(Return(-1));
  ipsec_connection_->InvokeScheduleConnectTask(
      ConnectStep::kStrongSwanConfigWritten);

  EXPECT_CALL(callbacks_, OnFailure(_));
  dispatcher_.task_environment().RunUntilIdle();
}

TEST_F(IPsecConnectionTest, StartCharonFailWithCharonExited) {
  ipsec_connection_->set_state(VPNConnection::State::kConnecting);

  base::OnceCallback<void(int)> exit_cb;
  EXPECT_CALL(process_manager_, StartProcessInMinijail(_, _, _, _, _, _))
      .WillOnce(
          WithArg<5>([&exit_cb](base::OnceCallback<void(int)> exit_callback) {
            exit_cb = std::move(exit_callback);
            return 123;
          }));
  ipsec_connection_->InvokeScheduleConnectTask(
      ConnectStep::kStrongSwanConfigWritten);

  std::move(exit_cb).Run(1);

  EXPECT_CALL(callbacks_, OnFailure(_));
  dispatcher_.task_environment().RunUntilIdle();
}

TEST_F(IPsecConnectionTest, WriteSwanctlConfig) {
  base::FilePath temp_dir = ipsec_connection_->SetTempDir();

  // Creates a config with PSK. Cert will be covered by tast tests.
  auto config = std::make_unique<IPsecConnection::Config>();
  config->remote = "10.0.0.1";
  config->local_proto_port = "17/1701";
  config->remote_proto_port = "17/1701";
  config->psk = "this is psk";
  config->xauth_user = "xauth_user";
  config->xauth_password = "xauth_password";
  ipsec_connection_->set_config(std::move(config));

  // Signal should be sent out at the end of the execution.
  EXPECT_CALL(*ipsec_connection_,
              ScheduleConnectTask(ConnectStep::kSwanctlConfigWritten));

  ipsec_connection_->InvokeScheduleConnectTask(ConnectStep::kCharonStarted);

  // IPsecConnection should write the config to the `swanctl.conf` file under
  // the temp dir it created.
  base::FilePath expected_path = temp_dir.Append("swanctl.conf");
  ASSERT_TRUE(base::PathExists(expected_path));
  std::string actual_content;
  ASSERT_TRUE(base::ReadFileToString(expected_path, &actual_content));
  EXPECT_EQ(actual_content, kExpectedSwanctlConfWithPSK);

  // The file should be deleted after destroying the IPsecConnection object.
  ipsec_connection_ = nullptr;
  ASSERT_FALSE(base::PathExists(expected_path));
}

TEST_F(IPsecConnectionTest, SwanctlLoadConfig) {
  const base::FilePath kStrongSwanConfPath("/tmp/strongswan.conf");
  ipsec_connection_->set_strongswan_conf_path(kStrongSwanConfPath);

  const base::FilePath kSwanctlConfPath("/tmp/swanctl.conf");
  ipsec_connection_->set_swanctl_conf_path(kSwanctlConfPath);

  // Expects call for starting swanctl process.
  base::OnceCallback<void(int)> exit_cb;
  const base::FilePath kExpectedProgramPath("/usr/sbin/swanctl");
  const std::vector<std::string> kExpectedArgs = {"--load-all", "--file",
                                                  kSwanctlConfPath.value()};
  const std::map<std::string, std::string> kExpectedEnv = {
      {"STRONGSWAN_CONF", kStrongSwanConfPath.value()}};
  constexpr uint64_t kExpectedCapMask = 0;
  EXPECT_CALL(process_manager_,
              StartProcessInMinijail(
                  _, kExpectedProgramPath, kExpectedArgs, kExpectedEnv,
                  AllOf(MinijailOptionsMatchUserGroup("vpn", "vpn"),
                        MinijailOptionsMatchCapMask(kExpectedCapMask),
                        MinijailOptionsMatchInheritSupplumentaryGroup(true),
                        MinijailOptionsMatchCloseNonstdFDs(true)),
                  _))
      .WillOnce(
          WithArg<5>([&exit_cb](base::OnceCallback<void(int)> exit_callback) {
            exit_cb = std::move(exit_callback);
            return 123;
          }));

  ipsec_connection_->InvokeScheduleConnectTask(
      ConnectStep::kSwanctlConfigWritten);

  // Signal should be sent out if swanctl exits with 0.
  EXPECT_CALL(*ipsec_connection_,
              ScheduleConnectTask(ConnectStep::kSwanctlConfigLoaded));
  std::move(exit_cb).Run(0);
}

TEST_F(IPsecConnectionTest, SwanctlLoadConfigFailExecution) {
  ipsec_connection_->set_state(VPNConnection::State::kConnecting);

  EXPECT_CALL(process_manager_, StartProcessInMinijail(_, _, _, _, _, _))
      .WillOnce(Return(-1));
  ipsec_connection_->InvokeScheduleConnectTask(
      ConnectStep::kSwanctlConfigWritten);

  EXPECT_CALL(callbacks_, OnFailure(_));
  dispatcher_.task_environment().RunUntilIdle();
}

TEST_F(IPsecConnectionTest, SwanctlLoadConfigFailExitCodeNonZero) {
  ipsec_connection_->set_state(VPNConnection::State::kConnecting);

  base::OnceCallback<void(int)> exit_cb;
  EXPECT_CALL(process_manager_, StartProcessInMinijail(_, _, _, _, _, _))
      .WillOnce(
          WithArg<5>([&exit_cb](base::OnceCallback<void(int)> exit_callback) {
            exit_cb = std::move(exit_callback);
            return 123;
          }));

  ipsec_connection_->InvokeScheduleConnectTask(
      ConnectStep::kSwanctlConfigWritten);

  std::move(exit_cb).Run(1);

  EXPECT_CALL(callbacks_, OnFailure(_));
  dispatcher_.task_environment().RunUntilIdle();
}

TEST_F(IPsecConnectionTest, SwanctlInitiateConnection) {
  const base::FilePath kStrongSwanConfPath("/tmp/strongswan.conf");
  ipsec_connection_->set_strongswan_conf_path(kStrongSwanConfPath);

  const base::FilePath kSwanctlConfPath("/tmp/swanctl.conf");
  ipsec_connection_->set_swanctl_conf_path(kSwanctlConfPath);

  // Expects call for starting swanctl process.
  base::OnceCallback<void(int)> exit_cb;
  const base::FilePath kExpectedProgramPath("/usr/sbin/swanctl");
  const std::vector<std::string> kExpectedArgs = {"--initiate", "-c", "managed",
                                                  "--timeout", "30"};
  const std::map<std::string, std::string> kExpectedEnv = {
      {"STRONGSWAN_CONF", kStrongSwanConfPath.value()}};
  constexpr uint64_t kExpectedCapMask = 0;
  EXPECT_CALL(process_manager_,
              StartProcessInMinijail(
                  _, kExpectedProgramPath, kExpectedArgs, kExpectedEnv,
                  AllOf(MinijailOptionsMatchUserGroup("vpn", "vpn"),
                        MinijailOptionsMatchCapMask(kExpectedCapMask),
                        MinijailOptionsMatchInheritSupplumentaryGroup(true),
                        MinijailOptionsMatchCloseNonstdFDs(true)),
                  _))
      .WillOnce(
          WithArg<5>([&exit_cb](base::OnceCallback<void(int)> exit_callback) {
            exit_cb = std::move(exit_callback);
            return 123;
          }));

  ipsec_connection_->InvokeScheduleConnectTask(
      ConnectStep::kSwanctlConfigLoaded);

  // Signal should be sent out if swanctl exits with 0.
  EXPECT_CALL(*ipsec_connection_,
              ScheduleConnectTask(ConnectStep::kIPsecConnected));
  std::move(exit_cb).Run(0);
}

TEST_F(IPsecConnectionTest, StartL2TPLayerAndConnected) {
  ipsec_connection_->set_state(VPNConnection::State::kConnecting);
  // L2TP connect.
  ipsec_connection_->InvokeScheduleConnectTask(ConnectStep::kIPsecConnected);
  EXPECT_CALL(*l2tp_connection_, OnConnect());
  dispatcher_.task_environment().RunUntilIdle();

  // L2TP connected.
  const std::string kIfName = "ppp0";
  constexpr int kIfIndex = 123;
  const IPConfig::Properties kIPProperties;
  l2tp_connection_->TriggerConnected(kIfName, kIfIndex, kIPProperties);

  EXPECT_CALL(callbacks_, OnConnected(kIfName, kIfIndex, _));
  dispatcher_.task_environment().RunUntilIdle();
}

TEST_F(IPsecConnectionTest, OnL2TPFailure) {
  ipsec_connection_->set_state(VPNConnection::State::kConnected);
  l2tp_connection_->set_state(VPNConnection::State::kConnecting);
  l2tp_connection_->TriggerFailure(Service::kFailureInternal, "");

  EXPECT_CALL(callbacks_, OnFailure(Service::kFailureInternal));
  dispatcher_.task_environment().RunUntilIdle();
}

TEST_F(IPsecConnectionTest, OnL2TPStopped) {
  ipsec_connection_->set_state(VPNConnection::State::kDisconnecting);
  l2tp_connection_->set_state(VPNConnection::State::kDisconnecting);
  l2tp_connection_->TriggerStopped();

  // If charon is still running, it should be stopped.
  constexpr pid_t kCharonPid = 123;
  ipsec_connection_->set_charon_pid(kCharonPid);
  EXPECT_CALL(process_manager_, StopProcess(kCharonPid));

  EXPECT_CALL(callbacks_, OnStopped());
  dispatcher_.task_environment().RunUntilIdle();
}

}  // namespace
}  // namespace shill
