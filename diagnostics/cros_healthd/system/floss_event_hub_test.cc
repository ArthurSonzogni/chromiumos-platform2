// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <base/test/gmock_callback_support.h>
#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <dbus/object_path.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "diagnostics/cros_healthd/system/fake_floss_event_hub.h"
#include "diagnostics/dbus_bindings/bluetooth_manager/dbus-proxy-mocks.h"
#include "diagnostics/dbus_bindings/floss/dbus-proxy-mocks.h"

namespace diagnostics {
namespace {

using ::testing::ReturnRef;
using ::testing::StrictMock;

// Tests for the FlossEventHub class.
class FlossEventHubTest : public testing::Test {
 protected:
  FlossEventHubTest() = default;
  FlossEventHubTest(const FlossEventHubTest&) = delete;
  FlossEventHubTest& operator=(const FlossEventHubTest&) = delete;

  FakeFlossEventHub* fake_floss_event_hub() const {
    return fake_floss_event_hub_.get();
  }

  // Mock proxy.
  StrictMock<org::chromium::bluetooth::BluetoothProxyMock> mock_adapter_proxy_;
  StrictMock<org::chromium::bluetooth::ManagerProxyMock> mock_manager_proxy_;
  StrictMock<org::chromium::bluetooth::BluetoothGattProxyMock> mock_gatt_proxy_;

 private:
  base::test::TaskEnvironment task_environment_;
  std::unique_ptr<FakeFlossEventHub> fake_floss_event_hub_ =
      std::make_unique<FakeFlossEventHub>();
};

}  // namespace

// Test that we will observe that BluetoothProxy register callback service when
// the adapter is added.
TEST_F(FlossEventHubTest, ObserveAdapterRegisterCallback) {
  const auto path = dbus::ObjectPath("/org/chromium/bluetooth/hci0/adapter");
  EXPECT_CALL(mock_adapter_proxy_, GetObjectPath).WillOnce(ReturnRef(path));
  EXPECT_CALL(mock_adapter_proxy_, RegisterCallbackAsync)
      .WillOnce(base::test::RunOnceCallback<1>(0));
  EXPECT_CALL(mock_adapter_proxy_, RegisterConnectionCallbackAsync)
      .WillOnce(base::test::RunOnceCallback<1>(0));

  fake_floss_event_hub()->SendAdapterAdded(&mock_adapter_proxy_);
}

// Test that we will observe that ManagerProxy register callback service when
// the manager is added.
TEST_F(FlossEventHubTest, ObserveManagerRegisterCallback) {
  EXPECT_CALL(mock_manager_proxy_, RegisterCallbackAsync)
      .WillOnce(base::test::RunOnceCallback<1>());

  fake_floss_event_hub()->SendManagerAdded(&mock_manager_proxy_);
}

// Test that we will observe that BluetoothGattProxy register callback service
// when the adapter gatt is added.
TEST_F(FlossEventHubTest, ObserveBluetoothGattRegisterCallback) {
  const auto path = dbus::ObjectPath("/org/chromium/bluetooth/hci0/gatt");
  EXPECT_CALL(mock_gatt_proxy_, GetObjectPath).WillOnce(ReturnRef(path));
  EXPECT_CALL(mock_gatt_proxy_, RegisterScannerCallbackAsync)
      .WillOnce(base::test::RunOnceCallback<1>(0));

  fake_floss_event_hub()->SendAdapterGattAdded(&mock_gatt_proxy_);
}

}  // namespace diagnostics
