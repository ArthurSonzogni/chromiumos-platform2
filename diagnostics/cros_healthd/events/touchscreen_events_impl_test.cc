// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include <base/test/task_environment.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <mojo/public/cpp/bindings/pending_remote.h>

#include "diagnostics/cros_healthd/events/event_observer_test_future.h"
#include "diagnostics/cros_healthd/events/touchscreen_events_impl.h"
#include "diagnostics/cros_healthd/executor/utils/fake_process_control.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "diagnostics/mojom/public/cros_healthd_events.mojom.h"

namespace diagnostics {
namespace {

namespace mojom = ::ash::cros_healthd::mojom;

using ::testing::_;

// Tests for the TouchscreenEventsImpl class.
class TouchscreenEventsImplTest : public testing::Test {
 public:
  TouchscreenEventsImplTest(const TouchscreenEventsImplTest&) = delete;
  TouchscreenEventsImplTest& operator=(const TouchscreenEventsImplTest&) =
      delete;

 protected:
  TouchscreenEventsImplTest() = default;

  void SetUp() override {
    EXPECT_CALL(*mock_executor(), MonitorTouchscreen(_, _))
        .WillOnce(
            [=, this](auto touchscreen_observer, auto pending_process_control) {
              touchscreen_observer_.Bind(std::move(touchscreen_observer));
              process_control_.BindReceiver(std::move(pending_process_control));
            });
  }

  void AddEventObserver(mojo::PendingRemote<mojom::EventObserver> observer) {
    events_impl_.AddObserver(std::move(observer));
  }

  void EmitTouchscreenConnectedEvent(
      const mojom::TouchscreenConnectedEventPtr& event) {
    touchscreen_observer_->OnConnected(event.Clone());
  }

  void EmitTouchscreenTouchEvent(const mojom::TouchscreenTouchEventPtr& event) {
    touchscreen_observer_->OnTouch(event.Clone());
  }

  mojo::Remote<mojom::TouchscreenObserver> touchscreen_observer_;
  FakeProcessControl process_control_;

 private:
  MockExecutor* mock_executor() { return mock_context_.mock_executor(); }

  base::test::TaskEnvironment task_environment_;
  MockContext mock_context_;
  TouchscreenEventsImpl events_impl_{&mock_context_};
};

// Test that we can receive touchscreen touch events.
TEST_F(TouchscreenEventsImplTest, TouchscreenTouchEvent) {
  mojom::TouchscreenTouchEvent fake_touch_event;
  fake_touch_event.touch_points.push_back(mojom::TouchPointInfo::New());

  EventObserverTestFuture event_observer;
  AddEventObserver(event_observer.BindNewPendingRemote());

  EmitTouchscreenTouchEvent(fake_touch_event.Clone());

  auto event = event_observer.WaitForEvent();
  ASSERT_TRUE(event->is_touchscreen_event_info());
  const auto& touchscreen_event_info = event->get_touchscreen_event_info();
  ASSERT_TRUE(touchscreen_event_info->is_touch_event());
  EXPECT_EQ(fake_touch_event, *touchscreen_event_info->get_touch_event());
}

// Test that we can receive touchscreen connected events.
TEST_F(TouchscreenEventsImplTest, TouchscreenConnectedEvent) {
  mojom::TouchscreenConnectedEvent fake_connected_event;
  fake_connected_event.max_x = 1;
  fake_connected_event.max_y = 2;

  EventObserverTestFuture event_observer;
  AddEventObserver(event_observer.BindNewPendingRemote());

  EmitTouchscreenConnectedEvent(fake_connected_event.Clone());

  auto event = event_observer.WaitForEvent();
  ASSERT_TRUE(event->is_touchscreen_event_info());
  const auto& touchscreen_event_info = event->get_touchscreen_event_info();
  ASSERT_TRUE(touchscreen_event_info->is_connected_event());
  EXPECT_EQ(fake_connected_event,
            *touchscreen_event_info->get_connected_event());
}

// Test that we can receive touchscreen connected events by multiple observers.
TEST_F(TouchscreenEventsImplTest,
       TouchscreenConnectedEventWithMultipleObservers) {
  mojom::TouchscreenConnectedEvent fake_connected_event;
  fake_connected_event.max_x = 1;
  fake_connected_event.max_y = 2;

  EventObserverTestFuture event_observer, event_observer2;
  AddEventObserver(event_observer.BindNewPendingRemote());
  AddEventObserver(event_observer2.BindNewPendingRemote());

  EmitTouchscreenConnectedEvent(fake_connected_event.Clone());

  auto check_result = [&fake_connected_event](mojom::EventInfoPtr event) {
    ASSERT_TRUE(event->is_touchscreen_event_info());
    const auto& touchscreen_event_info = event->get_touchscreen_event_info();
    ASSERT_TRUE(touchscreen_event_info->is_connected_event());
    EXPECT_EQ(fake_connected_event,
              *touchscreen_event_info->get_connected_event());
  };

  check_result(event_observer.WaitForEvent());
  check_result(event_observer2.WaitForEvent());
}

// Test that process control is reset when delegate observer disconnects.
TEST_F(TouchscreenEventsImplTest,
       ProcessControlResetWhenDelegateObserverDisconnects) {
  EventObserverTestFuture event_observer;
  AddEventObserver(event_observer.BindNewPendingRemote());

  process_control_.receiver().FlushForTesting();
  EXPECT_TRUE(process_control_.IsConnected());

  // Simulate the disconnection of delegate observer.
  touchscreen_observer_.FlushForTesting();
  touchscreen_observer_.reset();

  process_control_.receiver().FlushForTesting();
  EXPECT_FALSE(process_control_.IsConnected());
}

// Test that process control is reset when there is no event observer.
TEST_F(TouchscreenEventsImplTest, ProcessControlResetWhenNoEventObserver) {
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
