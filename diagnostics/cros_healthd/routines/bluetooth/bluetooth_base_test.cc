// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/test/bind.h>
#include <base/test/task_environment.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "diagnostics/cros_healthd/routines/bluetooth/bluetooth_base.h"
#include "diagnostics/cros_healthd/system/mock_bluetooth_info_manager.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "diagnostics/dbus_bindings/bluetooth/dbus-proxy-mocks.h"

namespace diagnostics {
namespace {

namespace mojom = ::ash::cros_healthd::mojom;

using ::testing::_;
using ::testing::Invoke;
using ::testing::Return;
using ::testing::StrictMock;

class BluetoothRoutineBaseTest : public testing::Test {
 protected:
  BluetoothRoutineBaseTest() = default;
  BluetoothRoutineBaseTest(const BluetoothRoutineBaseTest&) = delete;
  BluetoothRoutineBaseTest& operator=(const BluetoothRoutineBaseTest&) = delete;

  MockContext* mock_context() { return &mock_context_; }

  MockBluetoothInfoManager* mock_bluetooth_info_manager() {
    return mock_context_.mock_bluetooth_info_manager();
  }

  // Getter of mock proxy.
  org::bluez::Adapter1ProxyMock* mock_adapter_proxy() const {
    return static_cast<StrictMock<org::bluez::Adapter1ProxyMock>*>(
        adapter_proxy_.get());
  }

 private:
  base::test::TaskEnvironment task_environment_;
  MockContext mock_context_;
  // Mock proxy.
  std::unique_ptr<org::bluez::Adapter1ProxyMock> adapter_proxy_ =
      std::make_unique<StrictMock<org::bluez::Adapter1ProxyMock>>();
};

// Test that the BluetoothRoutineBase can get adapter successfully.
TEST_F(BluetoothRoutineBaseTest, GetAdapterSuccess) {
  EXPECT_CALL(*mock_bluetooth_info_manager(), GetAdapters())
      .WillOnce(Return(std::vector<org::bluez::Adapter1ProxyInterface*>{
          mock_adapter_proxy()}));
  auto routine_base = std::make_unique<BluetoothRoutineBase>(mock_context());
  EXPECT_EQ(routine_base->GetAdapter(), mock_adapter_proxy());
}

// Test that the BluetoothRoutineBase can handle empty adapters and return null.
TEST_F(BluetoothRoutineBaseTest, EmptyAdapter) {
  EXPECT_CALL(*mock_bluetooth_info_manager(), GetAdapters())
      .WillOnce(Return(std::vector<org::bluez::Adapter1ProxyInterface*>{}));
  auto routine_base = std::make_unique<BluetoothRoutineBase>(mock_context());
  ASSERT_EQ(routine_base->GetAdapter(), nullptr);
}

// Test that the BluetoothRoutineBase can handle null adapter and return null.
TEST_F(BluetoothRoutineBaseTest, NullAdapter) {
  EXPECT_CALL(*mock_bluetooth_info_manager(), GetAdapters())
      .WillOnce(Return(std::vector<org::bluez::Adapter1ProxyInterface*>{
          nullptr, mock_adapter_proxy()}));
  auto routine_base = std::make_unique<BluetoothRoutineBase>(mock_context());
  ASSERT_EQ(routine_base->GetAdapter(), nullptr);
}

// Test that the BluetoothRoutineBase can ensure the adapter is powered on
// successfully.
TEST_F(BluetoothRoutineBaseTest, AdapterPoweredOnSuccess) {
  base::RunLoop run_loop;
  EXPECT_CALL(*mock_adapter_proxy(), powered()).WillOnce(Return(false));
  EXPECT_CALL(*mock_adapter_proxy(), set_powered(_, _))
      .WillOnce(
          Invoke([](bool powered, base::OnceCallback<void(bool)> on_finish) {
            EXPECT_TRUE(powered);
            std::move(on_finish).Run(true);
          }));
  EXPECT_CALL(*mock_bluetooth_info_manager(), GetAdapters())
      .WillOnce(Return(std::vector<org::bluez::Adapter1ProxyInterface*>{
          mock_adapter_proxy()}));

  auto routine_base = std::make_unique<BluetoothRoutineBase>(mock_context());
  routine_base->EnsureAdapterPoweredOn(
      base::BindLambdaForTesting([&](bool is_success) {
        EXPECT_TRUE(is_success);
        run_loop.Quit();
      }));
  run_loop.Run();
}

// Test that the BluetoothRoutineBase can ensure the adapter is powered on
// successfully when the adapter is already powered on.
TEST_F(BluetoothRoutineBaseTest, AdapterAlreadyPoweredOn) {
  base::RunLoop run_loop;
  EXPECT_CALL(*mock_adapter_proxy(), powered()).WillOnce(Return(true));
  EXPECT_CALL(*mock_bluetooth_info_manager(), GetAdapters())
      .WillOnce(Return(std::vector<org::bluez::Adapter1ProxyInterface*>{
          mock_adapter_proxy()}));

  auto routine_base = std::make_unique<BluetoothRoutineBase>(mock_context());
  routine_base->EnsureAdapterPoweredOn(
      base::BindLambdaForTesting([&](bool is_success) {
        EXPECT_TRUE(is_success);
        run_loop.Quit();
      }));
  run_loop.Run();
}

// Test that the BluetoothRoutineBase can handle null adapter when powering on
// the adapter.
TEST_F(BluetoothRoutineBaseTest, NoAdapterPoweredOn) {
  base::RunLoop run_loop;
  EXPECT_CALL(*mock_bluetooth_info_manager(), GetAdapters())
      .WillOnce(
          Return(std::vector<org::bluez::Adapter1ProxyInterface*>{nullptr}));

  auto routine_base = std::make_unique<BluetoothRoutineBase>(mock_context());
  routine_base->EnsureAdapterPoweredOn(
      base::BindLambdaForTesting([&](bool is_success) {
        EXPECT_FALSE(is_success);
        run_loop.Quit();
      }));
  run_loop.Run();
}

}  // namespace
}  // namespace diagnostics
