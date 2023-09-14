// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/test/gmock_callback_support.h>
#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "diagnostics/cros_healthd/routines/bluetooth/bluetooth_base.h"
#include "diagnostics/cros_healthd/routines/bluetooth/bluetooth_constants.h"
#include "diagnostics/cros_healthd/system/mock_bluez_controller.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "diagnostics/dbus_bindings/bluez/dbus-proxy-mocks.h"

namespace diagnostics {
namespace {

namespace mojom = ::ash::cros_healthd::mojom;

using ::testing::_;
using ::testing::InSequence;
using ::testing::Return;
using ::testing::StrictMock;

bool EnsureAdapterPoweredStateSync(BluetoothRoutineBase* const routine_base,
                                   bool powered) {
  base::test::TestFuture<bool> future;
  routine_base->EnsureAdapterPoweredState(powered, future.GetCallback());
  return future.Get();
}

class BluetoothRoutineBaseTest : public testing::Test {
 protected:
  BluetoothRoutineBaseTest() = default;
  BluetoothRoutineBaseTest(const BluetoothRoutineBaseTest&) = delete;
  BluetoothRoutineBaseTest& operator=(const BluetoothRoutineBaseTest&) = delete;

  MockBluezController* mock_bluez_controller() {
    return mock_context_.mock_bluez_controller();
  }

  MockContext mock_context_;
  StrictMock<org::bluez::Adapter1ProxyMock> mock_adapter_proxy_;

 private:
  base::test::TaskEnvironment task_environment_;
};

// Test that the BluetoothRoutineBase can get adapter successfully.
TEST_F(BluetoothRoutineBaseTest, GetAdapterSuccess) {
  EXPECT_CALL(*mock_bluez_controller(), GetAdapters())
      .WillOnce(Return(std::vector<org::bluez::Adapter1ProxyInterface*>{
          &mock_adapter_proxy_}));
  auto routine_base = std::make_unique<BluetoothRoutineBase>(&mock_context_);
  EXPECT_EQ(routine_base->GetAdapter(), &mock_adapter_proxy_);
}

// Test that the BluetoothRoutineBase can handle empty adapters and return null.
TEST_F(BluetoothRoutineBaseTest, EmptyAdapter) {
  EXPECT_CALL(*mock_bluez_controller(), GetAdapters())
      .WillOnce(Return(std::vector<org::bluez::Adapter1ProxyInterface*>{}));
  auto routine_base = std::make_unique<BluetoothRoutineBase>(&mock_context_);
  ASSERT_EQ(routine_base->GetAdapter(), nullptr);
}

// Test that the BluetoothRoutineBase can handle null adapter and return null.
TEST_F(BluetoothRoutineBaseTest, NullAdapter) {
  EXPECT_CALL(*mock_bluez_controller(), GetAdapters())
      .WillOnce(Return(std::vector<org::bluez::Adapter1ProxyInterface*>{
          nullptr, &mock_adapter_proxy_}));
  auto routine_base = std::make_unique<BluetoothRoutineBase>(&mock_context_);
  ASSERT_EQ(routine_base->GetAdapter(), nullptr);
}

// Test that the BluetoothRoutineBase can ensure the adapter is powered on
// successfully.
TEST_F(BluetoothRoutineBaseTest, EnsureAdapterPowerOnSuccess) {
  InSequence s;
  EXPECT_CALL(*mock_bluez_controller(), GetAdapters())
      .WillOnce(Return(std::vector<org::bluez::Adapter1ProxyInterface*>{
          &mock_adapter_proxy_}));
  EXPECT_CALL(mock_adapter_proxy_, powered()).WillOnce(Return(false));
  EXPECT_CALL(mock_adapter_proxy_, set_powered(true, _))
      .WillOnce(base::test::RunOnceCallback<1>(true));

  auto routine_base = std::make_unique<BluetoothRoutineBase>(&mock_context_);
  EXPECT_TRUE(EnsureAdapterPoweredStateSync(routine_base.get(), true));
}

// Test that the BluetoothRoutineBase can ensure the adapter is powered off
// successfully.
TEST_F(BluetoothRoutineBaseTest, EnsureAdapterPowerOffSuccess) {
  InSequence s;
  EXPECT_CALL(*mock_bluez_controller(), GetAdapters())
      .WillOnce(Return(std::vector<org::bluez::Adapter1ProxyInterface*>{
          &mock_adapter_proxy_}));
  EXPECT_CALL(mock_adapter_proxy_, powered()).WillOnce(Return(true));
  EXPECT_CALL(mock_adapter_proxy_, set_powered(false, _))
      .WillOnce(base::test::RunOnceCallback<1>(true));

  auto routine_base = std::make_unique<BluetoothRoutineBase>(&mock_context_);
  EXPECT_TRUE(EnsureAdapterPoweredStateSync(routine_base.get(), false));
}

// Test that the BluetoothRoutineBase can ensure the adapter is powered on
// successfully when the adapter is already powered on.
TEST_F(BluetoothRoutineBaseTest, AdapterAlreadyPoweredOn) {
  InSequence s;
  EXPECT_CALL(*mock_bluez_controller(), GetAdapters())
      .WillOnce(Return(std::vector<org::bluez::Adapter1ProxyInterface*>{
          &mock_adapter_proxy_}));
  EXPECT_CALL(mock_adapter_proxy_, powered()).WillOnce(Return(true));

  auto routine_base = std::make_unique<BluetoothRoutineBase>(&mock_context_);
  EXPECT_TRUE(EnsureAdapterPoweredStateSync(routine_base.get(), true));
}

// Test that the BluetoothRoutineBase can handle null adapter when powering on
// the adapter.
TEST_F(BluetoothRoutineBaseTest, NoAdapterPoweredOn) {
  EXPECT_CALL(*mock_bluez_controller(), GetAdapters())
      .WillOnce(
          Return(std::vector<org::bluez::Adapter1ProxyInterface*>{nullptr}));

  auto routine_base = std::make_unique<BluetoothRoutineBase>(&mock_context_);
  EXPECT_FALSE(EnsureAdapterPoweredStateSync(routine_base.get(), true));
}

// Test that the BluetoothRoutineBase can pass the pre-check.
TEST_F(BluetoothRoutineBaseTest, PreCheckPassed) {
  InSequence s;
  EXPECT_CALL(*mock_bluez_controller(), GetAdapters())
      .WillOnce(Return(std::vector<org::bluez::Adapter1ProxyInterface*>{
          &mock_adapter_proxy_}));
  EXPECT_CALL(mock_adapter_proxy_, powered()).WillOnce(Return(true));
  EXPECT_CALL(mock_adapter_proxy_, discovering()).WillOnce(Return(false));

  base::test::TestFuture<void> future;
  auto routine_base = std::make_unique<BluetoothRoutineBase>(&mock_context_);
  routine_base->RunPreCheck(future.GetCallback(), base::DoNothing());

  EXPECT_TRUE(future.Wait());
}

// Test that the BluetoothRoutineBase can handle null adapter when running
// pre-check.
TEST_F(BluetoothRoutineBaseTest, PreCheckFailedNoAdapter) {
  InSequence s;
  EXPECT_CALL(*mock_bluez_controller(), GetAdapters())
      .WillOnce(
          Return(std::vector<org::bluez::Adapter1ProxyInterface*>{nullptr}));

  base::test::TestFuture<mojom::DiagnosticRoutineStatusEnum, const std::string&>
      future;
  auto routine_base = std::make_unique<BluetoothRoutineBase>(&mock_context_);
  routine_base->RunPreCheck(base::DoNothing(), future.GetCallback());

  auto [status, error_message] = future.Take();
  EXPECT_EQ(status, mojom::DiagnosticRoutineStatusEnum::kError);
  EXPECT_EQ(error_message, kBluetoothRoutineFailedGetAdapter);
}

// Test that the BluetoothRoutineBase can handle that the adapter is already in
// discovery mode when running pre-check.
TEST_F(BluetoothRoutineBaseTest, PreCheckFailedDiscoveringOn) {
  InSequence s;
  EXPECT_CALL(*mock_bluez_controller(), GetAdapters())
      .WillOnce(Return(std::vector<org::bluez::Adapter1ProxyInterface*>{
          &mock_adapter_proxy_}));
  EXPECT_CALL(mock_adapter_proxy_, powered()).WillOnce(Return(true));
  EXPECT_CALL(mock_adapter_proxy_, discovering()).WillOnce(Return(true));

  base::test::TestFuture<mojom::DiagnosticRoutineStatusEnum, const std::string&>
      future;
  auto routine_base = std::make_unique<BluetoothRoutineBase>(&mock_context_);
  routine_base->RunPreCheck(base::DoNothing(), future.GetCallback());

  auto [status, error_message] = future.Take();
  EXPECT_EQ(status, mojom::DiagnosticRoutineStatusEnum::kFailed);
  EXPECT_EQ(error_message, kBluetoothRoutineFailedDiscoveryMode);
}

}  // namespace
}  // namespace diagnostics
