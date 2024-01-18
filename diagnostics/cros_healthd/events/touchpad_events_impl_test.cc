// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/events/touchpad_events_impl.h"

#include <utility>

#include <base/test/task_environment.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <mojo/public/cpp/bindings/pending_remote.h>

#include "diagnostics/cros_healthd/events/event_observer_test_future.h"
#include "diagnostics/cros_healthd/executor/utils/fake_process_control.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "diagnostics/mojom/public/cros_healthd_events.mojom.h"

namespace diagnostics {
namespace {

namespace mojom = ::ash::cros_healthd::mojom;

using ::testing::_;

// Tests for the TouchpadEventsImpl class.
class TouchpadEventsImplTest : public testing::Test {
 public:
  TouchpadEventsImplTest(const TouchpadEventsImplTest&) = delete;
  TouchpadEventsImplTest& operator=(const TouchpadEventsImplTest&) = delete;

 protected:
  TouchpadEventsImplTest() = default;

  void SetUp() override {
    EXPECT_CALL(*mock_executor(), MonitorTouchpad(_, _))
        .WillOnce(
            [=, this](auto touchpad_observer, auto pending_process_control) {
              touchpad_observer_.Bind(std::move(touchpad_observer));
              process_control_.BindReceiver(std::move(pending_process_control));
            });
  }

  MockExecutor* mock_executor() { return mock_context_.mock_executor(); }

  void AddEventObserver(mojo::PendingRemote<mojom::EventObserver> observer) {
    touchpad_events_impl_.AddObserver(std::move(observer));
  }

  void EmitTouchpadConnectedEvent(
      const mojom::TouchpadConnectedEventPtr& event) {
    touchpad_observer_->OnConnected(event.Clone());
  }
  void EmitTouchpadTouchEvent(const mojom::TouchpadTouchEventPtr& event) {
    touchpad_observer_->OnTouch(event.Clone());
  }
  void EmitTouchpadButtonEvent(const mojom::TouchpadButtonEventPtr& event) {
    touchpad_observer_->OnButton(event.Clone());
  }

  mojo::Remote<mojom::TouchpadObserver> touchpad_observer_;
  FakeProcessControl process_control_;

 private:
  base::test::TaskEnvironment task_environment_;
  MockContext mock_context_;
  TouchpadEventsImpl touchpad_events_impl_{&mock_context_};
};

// Test that we can receive touchpad touch events.
TEST_F(TouchpadEventsImplTest, TouchpadTouchEvent) {
  mojom::TouchpadTouchEvent fake_touch_event;
  fake_touch_event.touch_points.push_back(mojom::TouchPointInfo::New());

  EventObserverTestFuture event_observer;
  AddEventObserver(event_observer.BindNewPendingRemote());

  EmitTouchpadTouchEvent(fake_touch_event.Clone());

  auto event = event_observer.WaitForEvent();
  ASSERT_TRUE(event->is_touchpad_event_info());
  const auto& touchpad_event_info = event->get_touchpad_event_info();
  ASSERT_TRUE(touchpad_event_info->is_touch_event());
  EXPECT_EQ(fake_touch_event, *touchpad_event_info->get_touch_event());
}

// Test that we can receive touchpad button events.
TEST_F(TouchpadEventsImplTest, TouchpadButtonEvent) {
  mojom::TouchpadButtonEvent fake_button_event;
  fake_button_event.button = mojom::InputTouchButton::kLeft;
  fake_button_event.pressed = true;

  EventObserverTestFuture event_observer;
  AddEventObserver(event_observer.BindNewPendingRemote());

  EmitTouchpadButtonEvent(fake_button_event.Clone());

  auto event = event_observer.WaitForEvent();
  ASSERT_TRUE(event->is_touchpad_event_info());
  const auto& touchpad_event_info = event->get_touchpad_event_info();
  EXPECT_TRUE(touchpad_event_info->is_button_event());
  EXPECT_EQ(fake_button_event, *touchpad_event_info->get_button_event());
}

// Test that we can receive touchpad connected events.
TEST_F(TouchpadEventsImplTest, TouchpadConnectedEvent) {
  mojom::TouchpadConnectedEvent fake_connected_event;
  fake_connected_event.max_x = 1;
  fake_connected_event.max_y = 2;
  fake_connected_event.buttons = {mojom::InputTouchButton::kLeft};

  EventObserverTestFuture event_observer;
  AddEventObserver(event_observer.BindNewPendingRemote());

  EmitTouchpadConnectedEvent(fake_connected_event.Clone());

  auto event = event_observer.WaitForEvent();
  const auto& touchpad_event_info = event->get_touchpad_event_info();
  EXPECT_TRUE(touchpad_event_info->is_connected_event());
  EXPECT_EQ(fake_connected_event, *touchpad_event_info->get_connected_event());
}

// Test that we can receive touchpad connected events by multiple observers.
TEST_F(TouchpadEventsImplTest, TouchpadConnectedEventWithMultipleObservers) {
  mojom::TouchpadConnectedEvent fake_connected_event;
  fake_connected_event.max_x = 1;
  fake_connected_event.max_y = 2;
  fake_connected_event.buttons = {mojom::InputTouchButton::kLeft};

  EventObserverTestFuture event_observer, event_observer2;
  AddEventObserver(event_observer.BindNewPendingRemote());
  AddEventObserver(event_observer2.BindNewPendingRemote());

  EmitTouchpadConnectedEvent(fake_connected_event.Clone());

  auto check_result = [&fake_connected_event](mojom::EventInfoPtr event) {
    ASSERT_TRUE(event->is_touchpad_event_info());
    const auto& touchpad_event_info = event->get_touchpad_event_info();
    ASSERT_TRUE(touchpad_event_info->is_connected_event());
    EXPECT_EQ(fake_connected_event,
              *touchpad_event_info->get_connected_event());
  };

  check_result(event_observer.WaitForEvent());
  check_result(event_observer2.WaitForEvent());
}

// Test that process control is reset when delegate observer disconnects.
TEST_F(TouchpadEventsImplTest,
       ProcessControlResetWhenDelegateObserverDisconnects) {
  EventObserverTestFuture event_observer;
  AddEventObserver(event_observer.BindNewPendingRemote());

  process_control_.receiver().FlushForTesting();
  EXPECT_TRUE(process_control_.IsConnected());

  // Simulate the disconnection of delegate observer.
  touchpad_observer_.FlushForTesting();
  touchpad_observer_.reset();

  process_control_.receiver().FlushForTesting();
  EXPECT_FALSE(process_control_.IsConnected());
}

// Test that process control is reset when there is no event observer.
TEST_F(TouchpadEventsImplTest, ProcessControlResetWhenNoEventObserver) {
  EventObserverTestFuture event_observer;
  AddEventObserver(event_observer.BindNewPendingRemote());

  process_control_.receiver().FlushForTesting();
  EXPECT_TRUE(process_control_.IsConnected());

  event_observer.Reset();

  process_control_.receiver().FlushForTesting();
  EXPECT_FALSE(process_control_.IsConnected());
}

}  // namespace
}  // namespace diagnostics
