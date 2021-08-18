// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/vpn/ipsec_connection.h"

#include <memory>
#include <string>
#include <utility>

#include <base/files/scoped_temp_dir.h>
#include <base/files/file_util.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "shill/test_event_dispatcher.h"
#include "shill/vpn/fake_vpn_util.h"

namespace shill {

class IPsecConnectionUnderTest : public IPsecConnection {
 public:
  IPsecConnectionUnderTest(std::unique_ptr<Config> config,
                           std::unique_ptr<Callbacks> callbacks,
                           EventDispatcher* dispatcher)
      : IPsecConnection(std::move(config), std::move(callbacks), dispatcher) {
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

  MOCK_METHOD(void, ScheduleConnectTask, (ConnectStep), (override));
};

namespace {

using ConnectStep = IPsecConnection::ConnectStep;

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
    ipsec_connection_ = std::make_unique<IPsecConnectionUnderTest>(
        std::make_unique<IPsecConnection::Config>(), std::move(callbacks),
        &dispatcher_);
  }

 protected:
  EventDispatcherForTest dispatcher_;
  MockCallbacks callbacks_;
  std::unique_ptr<IPsecConnectionUnderTest> ipsec_connection_;
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
  // Signal should be send out at the end of the execution.
  EXPECT_CALL(*ipsec_connection_,
              ScheduleConnectTask(ConnectStep::kCharonStarted));

  ipsec_connection_->InvokeScheduleConnectTask(
      ConnectStep::kStrongSwanConfigWritten);
}

TEST_F(IPsecConnectionTest, WriteSwanctlConfig) {
  // Signal should be send out at the end of the execution.
  EXPECT_CALL(*ipsec_connection_,
              ScheduleConnectTask(ConnectStep::kSwanctlConfigWritten));

  ipsec_connection_->InvokeScheduleConnectTask(ConnectStep::kCharonStarted);
}

TEST_F(IPsecConnectionTest, SwanctlLoadConfig) {
  // Signal should be send out at the end of the execution.
  EXPECT_CALL(*ipsec_connection_,
              ScheduleConnectTask(ConnectStep::kSwanctlConfigLoaded));

  ipsec_connection_->InvokeScheduleConnectTask(
      ConnectStep::kSwanctlConfigWritten);
}

TEST_F(IPsecConnectionTest, SwanctlInitiateConnection) {
  // Signal should be send out at the end of the execution.
  EXPECT_CALL(*ipsec_connection_,
              ScheduleConnectTask(ConnectStep::kIPsecConnected));

  ipsec_connection_->InvokeScheduleConnectTask(
      ConnectStep::kSwanctlConfigLoaded);
}

}  // namespace
}  // namespace shill
