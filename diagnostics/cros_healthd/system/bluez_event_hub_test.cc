// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <base/test/gmock_callback_support.h>
#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <dbus/object_path.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "diagnostics/cros_healthd/system/bluez_event_hub.h"
#include "diagnostics/cros_healthd/system/fake_bluez_event_hub.h"
#include "diagnostics/dbus_bindings/bluez/dbus-proxy-mocks.h"

namespace diagnostics {
namespace {

using ::testing::StrictMock;

// Tests for the BluezEventHub class.
class BluezEventHubTest : public testing::Test {
 public:
  BluezEventHubTest(const BluezEventHubTest&) = delete;
  BluezEventHubTest& operator=(const BluezEventHubTest&) = delete;

 protected:
  BluezEventHubTest() = default;

  FakeBluezEventHub* fake_bluez_event_hub() const {
    return fake_bluez_event_hub_.get();
  }

  // Getter of mock proxy.
  org::bluez::Adapter1ProxyMock* mock_adapter_proxy() const {
    return static_cast<StrictMock<org::bluez::Adapter1ProxyMock>*>(
        adapter_proxy_.get());
  }
  org::bluez::Device1ProxyMock* mock_device_proxy() const {
    return static_cast<StrictMock<org::bluez::Device1ProxyMock>*>(
        device_proxy_.get());
  }

 private:
  base::test::TaskEnvironment task_environment_;
  std::unique_ptr<FakeBluezEventHub> fake_bluez_event_hub_ =
      std::make_unique<FakeBluezEventHub>();
  // Mock proxy.
  std::unique_ptr<org::bluez::Adapter1ProxyMock> adapter_proxy_ =
      std::make_unique<StrictMock<org::bluez::Adapter1ProxyMock>>();
  std::unique_ptr<org::bluez::Device1ProxyMock> device_proxy_ =
      std::make_unique<StrictMock<org::bluez::Device1ProxyMock>>();
};

}  // namespace

// Test that we will observe adapter property changed events when an adapter is
// added.
TEST_F(BluezEventHubTest, ObserveAdapterPropertyChanged) {
  base::test::TestFuture<void> future;
  EXPECT_CALL(*mock_adapter_proxy(), SetPropertyChangedCallback)
      .WillOnce(base::test::RunOnceClosure(future.GetCallback()));

  fake_bluez_event_hub()->SendAdapterAdded(mock_adapter_proxy());
  EXPECT_TRUE(future.Wait());
}

// Test that we will observe device property changed events when an device is
// added.
TEST_F(BluezEventHubTest, ObserveDevicePropertyChanged) {
  base::test::TestFuture<void> future;
  EXPECT_CALL(*mock_device_proxy(), SetPropertyChangedCallback)
      .WillOnce(base::test::RunOnceClosure(future.GetCallback()));

  fake_bluez_event_hub()->SendDeviceAdded(mock_device_proxy());
  EXPECT_TRUE(future.Wait());
}

}  // namespace diagnostics
