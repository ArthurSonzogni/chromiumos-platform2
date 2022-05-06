// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include <base/bind.h>
#include <base/check.h>
#include <base/run_loop.h>
#include <base/test/task_environment.h>
#include <dbus/object_path.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <mojo/public/cpp/bindings/pending_receiver.h>

#include "diagnostics/cros_healthd/events/bluetooth_events_impl.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "diagnostics/dbus_bindings/bluetooth/dbus-proxy-mocks.h"
#include "diagnostics/mojom/public/cros_healthd_events.mojom.h"

namespace diagnostics {
namespace {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

using ::testing::_;
using ::testing::Invoke;
using ::testing::StrictMock;

class MockCrosHealthdBluetoothObserver
    : public mojo_ipc::CrosHealthdBluetoothObserver {
 public:
  MockCrosHealthdBluetoothObserver(
      mojo::PendingReceiver<mojo_ipc::CrosHealthdBluetoothObserver> receiver)
      : receiver_{this /* impl */, std::move(receiver)} {
    DCHECK(receiver_.is_bound());
  }
  MockCrosHealthdBluetoothObserver(const MockCrosHealthdBluetoothObserver&) =
      delete;
  MockCrosHealthdBluetoothObserver& operator=(
      const MockCrosHealthdBluetoothObserver&) = delete;

  MOCK_METHOD(void, OnAdapterAdded, (), (override));
  MOCK_METHOD(void, OnAdapterRemoved, (), (override));
  MOCK_METHOD(void, OnAdapterPropertyChanged, (), (override));
  MOCK_METHOD(void, OnDeviceAdded, (), (override));
  MOCK_METHOD(void, OnDeviceRemoved, (), (override));
  MOCK_METHOD(void, OnDevicePropertyChanged, (), (override));

 private:
  mojo::Receiver<mojo_ipc::CrosHealthdBluetoothObserver> receiver_;
};

// Tests for the BluetoothEventsImpl class.
class BluetoothEventsImplTest : public testing::Test {
 protected:
  BluetoothEventsImplTest() = default;
  BluetoothEventsImplTest(const BluetoothEventsImplTest&) = delete;
  BluetoothEventsImplTest& operator=(const BluetoothEventsImplTest&) = delete;

  void SetUp() override {
    mojo::PendingRemote<mojo_ipc::CrosHealthdBluetoothObserver> observer;
    mojo::PendingReceiver<mojo_ipc::CrosHealthdBluetoothObserver>
        observer_receiver(observer.InitWithNewPipeAndPassReceiver());
    observer_ = std::make_unique<StrictMock<MockCrosHealthdBluetoothObserver>>(
        std::move(observer_receiver));
    bluetooth_events_impl_.AddObserver(std::move(observer));
  }

  BluetoothEventsImpl* bluetooth_events_impl() {
    return &bluetooth_events_impl_;
  }

  MockCrosHealthdBluetoothObserver* mock_observer() { return observer_.get(); }

  const dbus::ObjectPath adapter_path() {
    return dbus::ObjectPath("/org/bluez/hci0");
  }

  const dbus::ObjectPath device_path() {
    return dbus::ObjectPath("/org/bluez/hci0/dev_70_88_6B_92_34_70");
  }

  // Getter of mock proxy.
  org::bluez::Adapter1ProxyMock* mock_adapter_proxy() const {
    return static_cast<testing::StrictMock<org::bluez::Adapter1ProxyMock>*>(
        adapter_proxy_.get());
  }
  org::bluez::Device1ProxyMock* mock_device_proxy() const {
    return static_cast<testing::StrictMock<org::bluez::Device1ProxyMock>*>(
        device_proxy_.get());
  }

 private:
  base::test::TaskEnvironment task_environment_;

  MockContext mock_context_;
  BluetoothEventsImpl bluetooth_events_impl_{&mock_context_};
  std::unique_ptr<StrictMock<MockCrosHealthdBluetoothObserver>> observer_;
  // Mock proxy.
  std::unique_ptr<org::bluez::Adapter1ProxyMock> adapter_proxy_ =
      std::make_unique<testing::StrictMock<org::bluez::Adapter1ProxyMock>>();
  std::unique_ptr<org::bluez::Device1ProxyMock> device_proxy_ =
      std::make_unique<testing::StrictMock<org::bluez::Device1ProxyMock>>();
};

}  // namespace

// Test that we can receive an adapter added event.
TEST_F(BluetoothEventsImplTest, ReceiveAdapterAddedEvent) {
  base::RunLoop run_loop;
  EXPECT_CALL(*mock_observer(), OnAdapterAdded()).WillOnce(Invoke([&]() {
    run_loop.Quit();
  }));

  EXPECT_CALL(*mock_adapter_proxy(), SetPropertyChangedCallback(_)).Times(1);
  bluetooth_events_impl()->AdapterAdded(mock_adapter_proxy());
  run_loop.Run();
}

// Test that we can receive an adapter removed event.
TEST_F(BluetoothEventsImplTest, ReceiveAdapterRemovedEvent) {
  base::RunLoop run_loop;
  EXPECT_CALL(*mock_observer(), OnAdapterRemoved()).WillOnce(Invoke([&]() {
    run_loop.Quit();
  }));

  bluetooth_events_impl()->AdapterRemoved(adapter_path());
  run_loop.Run();
}

// Test that we can receive an adapter property changed event.
TEST_F(BluetoothEventsImplTest, ReceiveAdapterPropertyChangedEvent) {
  base::RunLoop run_loop;
  EXPECT_CALL(*mock_observer(), OnAdapterPropertyChanged())
      .WillOnce(Invoke([&]() { run_loop.Quit(); }));

  bluetooth_events_impl()->AdapterPropertyChanged(mock_adapter_proxy(), "");
  run_loop.Run();
}

// Test that we can receive a device added event.
TEST_F(BluetoothEventsImplTest, ReceiveDeviceAddedEvent) {
  base::RunLoop run_loop;
  EXPECT_CALL(*mock_observer(), OnDeviceAdded()).WillOnce(Invoke([&]() {
    run_loop.Quit();
  }));

  EXPECT_CALL(*mock_device_proxy(), SetPropertyChangedCallback(_)).Times(1);
  bluetooth_events_impl()->DeviceAdded(mock_device_proxy());
  run_loop.Run();
}

// Test that we can receive a device removed event.
TEST_F(BluetoothEventsImplTest, ReceiveDeviceRemovedEvent) {
  base::RunLoop run_loop;
  EXPECT_CALL(*mock_observer(), OnDeviceRemoved()).WillOnce(Invoke([&]() {
    run_loop.Quit();
  }));

  bluetooth_events_impl()->DeviceRemoved(device_path());
  run_loop.Run();
}

// Test that we can receive a device property changed event.
TEST_F(BluetoothEventsImplTest, ReceiveDevicePropertyChangedEvent) {
  base::RunLoop run_loop;
  EXPECT_CALL(*mock_observer(), OnDevicePropertyChanged())
      .WillOnce(Invoke([&]() { run_loop.Quit(); }));

  bluetooth_events_impl()->DevicePropertyChanged(mock_device_proxy(), "");
  run_loop.Run();
}

}  // namespace diagnostics
