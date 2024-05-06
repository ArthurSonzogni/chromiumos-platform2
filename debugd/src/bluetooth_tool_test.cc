// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "debugd/src/bluetooth_tool.h"

#include <memory>
#include <string>
#include <vector>

#include <chromeos/dbus/service_constants.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_object_proxy.h>
#include <gtest/gtest.h>

#include "debugd/src/sandboxed_process.h"

using testing::_;
using testing::Invoke;
using testing::Return;

namespace {

constexpr char kMockUserName[] = "mockName";
constexpr char kObfuscatedName[] = "obfuscatedName";

}  // namespace

namespace debugd {

class BluetoothToolForTesting : public BluetoothTool {
 public:
  using BluetoothTool::BluetoothTool;
  MOCK_METHOD(std::unique_ptr<SandboxedProcess>,
              CreateSandboxedProcess,
              (),
              (override));
};

class MockSandboxedProcess : public SandboxedProcess {
 public:
  using SandboxedProcess::SandboxedProcess;
  MOCK_METHOD(void, SandboxAs, (const std::string&, const std::string&));
  MOCK_METHOD(void, SetCapabilities, (uint64_t));
  MOCK_METHOD(void, SetSeccompFilterPolicyFile, (const std::string&));
  MOCK_METHOD(bool, Init, (const std::vector<std::string>&));
  MOCK_METHOD(void, AddArg, (const std::string&), (override));
  MOCK_METHOD(bool, Start, (), (override));
};

class BluetoothToolTest : public testing::Test {
 public:
  BluetoothToolTest() {
    scoped_refptr<dbus::MockBus> bus = new dbus::MockBus(dbus::Bus::Options{});
    proxy_ = new dbus::MockObjectProxy(
        bus.get(), login_manager::kSessionManagerServiceName,
        dbus::ObjectPath(login_manager::kSessionManagerServicePath));
    bluetooth_tool_ = std::make_unique<BluetoothToolForTesting>(bus);

    ON_CALL(*bus,
            GetObjectProxy(
                login_manager::kSessionManagerServiceName,
                dbus::ObjectPath(login_manager::kSessionManagerServicePath)))
        .WillByDefault(Return(proxy_.get()));
  }

  std::unique_ptr<dbus::Response> CreateUsernameResponseValid(
      dbus::MethodCall* method_call, int timeout_ms) {
    std::unique_ptr<dbus::Response> resp = dbus::Response::CreateEmpty();
    dbus::MessageWriter writer(resp.get());
    writer.AppendString(std::string(kMockUserName));
    writer.AppendString(std::string(kObfuscatedName));

    return resp;
  }

  std::unique_ptr<dbus::Response> CreateUsernameResponseEmpty(
      dbus::MethodCall* method_call, int timeout_ms) {
    std::unique_ptr<dbus::Response> resp = dbus::Response::CreateEmpty();
    dbus::MessageWriter writer(resp.get());
    writer.AppendString(std::string());
    writer.AppendString(std::string());

    return resp;
  }

  std::unique_ptr<SandboxedProcess> CreateSandboxedProcessValid() {
    std::unique_ptr<MockSandboxedProcess> mock =
        std::make_unique<MockSandboxedProcess>();
    ON_CALL(*mock, Init(_)).WillByDefault(Return(true));
    ON_CALL(*mock, Start()).WillByDefault(Return(true));

    return mock;
  }

  std::unique_ptr<SandboxedProcess> CreateSandboxedProcessFailed() {
    std::unique_ptr<MockSandboxedProcess> mock =
        std::make_unique<MockSandboxedProcess>();
    ON_CALL(*mock, Init(_)).WillByDefault(Return(false));

    return mock;
  }

 protected:
  std::unique_ptr<BluetoothToolForTesting> bluetooth_tool_;
  scoped_refptr<dbus::MockObjectProxy> proxy_;
};

// Success if username is valid and sandboxing is success
TEST_F(BluetoothToolTest, StartBtsnoop_Success) {
  EXPECT_CALL(*proxy_, CallMethodAndBlock(_, _))
      .WillOnce(Invoke(this, &BluetoothToolTest::CreateUsernameResponseValid));
  EXPECT_CALL(*bluetooth_tool_, CreateSandboxedProcess())
      .WillOnce(Invoke(this, &BluetoothToolTest::CreateSandboxedProcessValid));

  EXPECT_TRUE(bluetooth_tool_->StartBtsnoop());
  EXPECT_TRUE(bluetooth_tool_->IsBtsnoopRunning());
}

// Fail when username is empty (i.e. not signed in)
TEST_F(BluetoothToolTest, StartBtsnoop_NotSignedIn_Fail) {
  EXPECT_CALL(*proxy_, CallMethodAndBlock(_, _))
      .WillOnce(Invoke(this, &BluetoothToolTest::CreateUsernameResponseEmpty));

  EXPECT_FALSE(bluetooth_tool_->StartBtsnoop());
  EXPECT_FALSE(bluetooth_tool_->IsBtsnoopRunning());
}

// Fail when sandboxing has issues
TEST_F(BluetoothToolTest, StartBtsnoop_FailSandbox) {
  EXPECT_CALL(*proxy_, CallMethodAndBlock(_, _))
      .WillOnce(Invoke(this, &BluetoothToolTest::CreateUsernameResponseValid));
  EXPECT_CALL(*bluetooth_tool_, CreateSandboxedProcess())
      .WillOnce(Invoke(this, &BluetoothToolTest::CreateSandboxedProcessFailed));

  EXPECT_FALSE(bluetooth_tool_->StartBtsnoop());
  EXPECT_FALSE(bluetooth_tool_->IsBtsnoopRunning());
}

// Process should stop when Stop is called
TEST_F(BluetoothToolTest, StopBtsnoop_Success) {
  ON_CALL(*proxy_, CallMethodAndBlock(_, _))
      .WillByDefault(
          Invoke(this, &BluetoothToolTest::CreateUsernameResponseValid));
  ON_CALL(*bluetooth_tool_, CreateSandboxedProcess())
      .WillByDefault(
          Invoke(this, &BluetoothToolTest::CreateSandboxedProcessValid));

  bluetooth_tool_->StartBtsnoop();
  EXPECT_TRUE(bluetooth_tool_->IsBtsnoopRunning());
  bluetooth_tool_->StopBtsnoop();
  EXPECT_FALSE(bluetooth_tool_->IsBtsnoopRunning());
}

}  // namespace debugd
