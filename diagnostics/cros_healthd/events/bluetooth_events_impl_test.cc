// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include <base/check.h>
#include <base/run_loop.h>
#include <base/test/task_environment.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <mojo/public/cpp/bindings/pending_receiver.h>
#include <mojo/public/cpp/bindings/receiver.h>

#include "diagnostics/cros_healthd/events/bluetooth_events_impl.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "diagnostics/mojom/public/cros_healthd_events.mojom.h"

namespace diagnostics {
namespace {

namespace mojo_ipc = ::ash::cros_healthd::mojom;

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

  MockCrosHealthdBluetoothObserver* mock_observer() { return observer_.get(); }

  FakeBluetoothEventHub* fake_bluetooth_event_hub() const {
    return mock_context_.fake_bluetooth_event_hub();
  }

 private:
  base::test::TaskEnvironment task_environment_;

  MockContext mock_context_;
  BluetoothEventsImpl bluetooth_events_impl_{&mock_context_};
  std::unique_ptr<StrictMock<MockCrosHealthdBluetoothObserver>> observer_;
};

}  // namespace

// Test that we can receive an adapter added event.
TEST_F(BluetoothEventsImplTest, ReceiveAdapterAddedEvent) {
  base::RunLoop run_loop;
  EXPECT_CALL(*mock_observer(), OnAdapterAdded()).WillOnce(Invoke([&]() {
    run_loop.Quit();
  }));

  fake_bluetooth_event_hub()->SendAdapterAdded();
  run_loop.Run();
}

// Test that we can receive an adapter removed event.
TEST_F(BluetoothEventsImplTest, ReceiveAdapterRemovedEvent) {
  base::RunLoop run_loop;
  EXPECT_CALL(*mock_observer(), OnAdapterRemoved()).WillOnce(Invoke([&]() {
    run_loop.Quit();
  }));

  fake_bluetooth_event_hub()->SendAdapterRemoved();
  run_loop.Run();
}

// Test that we can receive an adapter property changed event.
TEST_F(BluetoothEventsImplTest, ReceiveAdapterPropertyChangedEvent) {
  base::RunLoop run_loop;
  EXPECT_CALL(*mock_observer(), OnAdapterPropertyChanged())
      .WillOnce(Invoke([&]() { run_loop.Quit(); }));

  fake_bluetooth_event_hub()->SendAdapterPropertyChanged();
  run_loop.Run();
}

// Test that we can receive a device added event.
TEST_F(BluetoothEventsImplTest, ReceiveDeviceAddedEvent) {
  base::RunLoop run_loop;
  EXPECT_CALL(*mock_observer(), OnDeviceAdded()).WillOnce(Invoke([&]() {
    run_loop.Quit();
  }));

  fake_bluetooth_event_hub()->SendDeviceAdded();
  run_loop.Run();
}

// Test that we can receive a device removed event.
TEST_F(BluetoothEventsImplTest, ReceiveDeviceRemovedEvent) {
  base::RunLoop run_loop;
  EXPECT_CALL(*mock_observer(), OnDeviceRemoved()).WillOnce(Invoke([&]() {
    run_loop.Quit();
  }));

  fake_bluetooth_event_hub()->SendDeviceRemoved();
  run_loop.Run();
}

// Test that we can receive a device property changed event.
TEST_F(BluetoothEventsImplTest, ReceiveDevicePropertyChangedEvent) {
  base::RunLoop run_loop;
  EXPECT_CALL(*mock_observer(), OnDevicePropertyChanged())
      .WillOnce(Invoke([&]() { run_loop.Quit(); }));

  fake_bluetooth_event_hub()->SendDevicePropertyChanged();
  run_loop.Run();
}

}  // namespace diagnostics
