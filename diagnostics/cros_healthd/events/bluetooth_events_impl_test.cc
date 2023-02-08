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
#include "diagnostics/cros_healthd/events/mock_event_observer.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "diagnostics/mojom/public/cros_healthd_events.mojom.h"

namespace diagnostics {
namespace {

namespace mojom = ::ash::cros_healthd::mojom;

using ::testing::_;
using ::testing::Invoke;
using ::testing::StrictMock;

// Tests for the BluetoothEventsImpl class.
class BluetoothEventsImplTest : public testing::Test {
 protected:
  BluetoothEventsImplTest() = default;
  BluetoothEventsImplTest(const BluetoothEventsImplTest&) = delete;
  BluetoothEventsImplTest& operator=(const BluetoothEventsImplTest&) = delete;

  void SetUp() override {
    mojo::PendingRemote<mojom::EventObserver> observer;
    mojo::PendingReceiver<mojom::EventObserver> observer_receiver(
        observer.InitWithNewPipeAndPassReceiver());
    observer_ = std::make_unique<StrictMock<MockEventObserver>>(
        std::move(observer_receiver));
    bluetooth_events_impl_.AddObserver(std::move(observer));
  }

  MockEventObserver* mock_observer() { return observer_.get(); }

  FakeBluetoothEventHub* fake_bluetooth_event_hub() const {
    return mock_context_.fake_bluetooth_event_hub();
  }

  void SetExpectedEvent(base::RunLoop& run_loop,
                        mojom::BluetoothEventInfo::State state) {
    EXPECT_CALL(*mock_observer(), OnEvent(_))
        .WillOnce(Invoke([&, state](mojom::EventInfoPtr info) mutable {
          EXPECT_TRUE(info->is_bluetooth_event_info());
          const auto& bluetooth_event_info = info->get_bluetooth_event_info();
          EXPECT_EQ(bluetooth_event_info->state, state);
          run_loop.Quit();
        }));
  }

 private:
  base::test::TaskEnvironment task_environment_;

  MockContext mock_context_;
  BluetoothEventsImpl bluetooth_events_impl_{&mock_context_};
  std::unique_ptr<StrictMock<MockEventObserver>> observer_;
};

}  // namespace

// Test that we can receive an adapter added event.
TEST_F(BluetoothEventsImplTest, ReceiveAdapterAddedEvent) {
  base::RunLoop run_loop;
  SetExpectedEvent(run_loop, mojom::BluetoothEventInfo::State::kAdapterAdded);

  fake_bluetooth_event_hub()->SendAdapterAdded();
  run_loop.Run();
}

// Test that we can receive an adapter removed event.
TEST_F(BluetoothEventsImplTest, ReceiveAdapterRemovedEvent) {
  base::RunLoop run_loop;
  SetExpectedEvent(run_loop, mojom::BluetoothEventInfo::State::kAdapterRemoved);

  fake_bluetooth_event_hub()->SendAdapterRemoved();
  run_loop.Run();
}

// Test that we can receive an adapter property changed event.
TEST_F(BluetoothEventsImplTest, ReceiveAdapterPropertyChangedEvent) {
  base::RunLoop run_loop;
  SetExpectedEvent(run_loop,
                   mojom::BluetoothEventInfo::State::kAdapterPropertyChanged);

  fake_bluetooth_event_hub()->SendAdapterPropertyChanged();
  run_loop.Run();
}

// Test that we can receive a device added event.
TEST_F(BluetoothEventsImplTest, ReceiveDeviceAddedEvent) {
  base::RunLoop run_loop;
  SetExpectedEvent(run_loop, mojom::BluetoothEventInfo::State::kDeviceAdded);

  fake_bluetooth_event_hub()->SendDeviceAdded();
  run_loop.Run();
}

// Test that we can receive a device removed event.
TEST_F(BluetoothEventsImplTest, ReceiveDeviceRemovedEvent) {
  base::RunLoop run_loop;
  SetExpectedEvent(run_loop, mojom::BluetoothEventInfo::State::kDeviceRemoved);

  fake_bluetooth_event_hub()->SendDeviceRemoved();
  run_loop.Run();
}

// Test that we can receive a device property changed event.
TEST_F(BluetoothEventsImplTest, ReceiveDevicePropertyChangedEvent) {
  base::RunLoop run_loop;
  SetExpectedEvent(run_loop,
                   mojom::BluetoothEventInfo::State::kDevicePropertyChanged);

  fake_bluetooth_event_hub()->SendDevicePropertyChanged();
  run_loop.Run();
}

}  // namespace diagnostics
