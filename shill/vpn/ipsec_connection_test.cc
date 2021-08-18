// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/vpn/ipsec_connection.h"

#include <memory>
#include <string>
#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "shill/test_event_dispatcher.h"

namespace shill {

class IPsecConnectionUnderTest : public IPsecConnection {
 public:
  IPsecConnectionUnderTest(std::unique_ptr<Config> config,
                           std::unique_ptr<Callbacks> callbacks,
                           EventDispatcher* dispatcher)
      : IPsecConnection(std::move(config), std::move(callbacks), dispatcher) {}

  IPsecConnectionUnderTest(const IPsecConnectionUnderTest&) = delete;
  IPsecConnectionUnderTest& operator=(const IPsecConnectionUnderTest&) = delete;

  void InvokeScheduleConnectTask(ConnectStep step) {
    IPsecConnection::ScheduleConnectTask(step);
  }

  MOCK_METHOD(void, ScheduleConnectTask, (ConnectStep), (override));
};

namespace {

using ConnectStep = IPsecConnection::ConnectStep;

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
  // Signal should be send out at the end of the execution.
  EXPECT_CALL(*ipsec_connection_,
              ScheduleConnectTask(ConnectStep::kStrongSwanConfigWritten));

  ipsec_connection_->InvokeScheduleConnectTask(ConnectStep::kStart);
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
