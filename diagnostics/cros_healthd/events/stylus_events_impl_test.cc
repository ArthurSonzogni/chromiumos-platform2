// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include <base/test/task_environment.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <mojo/public/cpp/bindings/pending_remote.h>

#include "diagnostics/cros_healthd/events/event_observer_test_future.h"
#include "diagnostics/cros_healthd/events/stylus_events_impl.h"
#include "diagnostics/cros_healthd/executor/utils/fake_process_control.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "diagnostics/mojom/public/cros_healthd_events.mojom.h"

namespace diagnostics {
namespace {

namespace mojom = ::ash::cros_healthd::mojom;

using ::testing::_;

// Tests for the StylusEventsImpl class.
class StylusEventsImplTest : public testing::Test {
 protected:
  StylusEventsImplTest() = default;
  StylusEventsImplTest(const StylusEventsImplTest&) = delete;
  StylusEventsImplTest& operator=(const StylusEventsImplTest&) = delete;

  void SetUp() override {
    EXPECT_CALL(*mock_executor(), MonitorStylus(_, _))
        .WillOnce([=](auto stylus_observer, auto pending_process_control) {
          stylus_observer_.Bind(std::move(stylus_observer));
          process_control_.BindReceiver(std::move(pending_process_control));
        });
  }

  MockExecutor* mock_executor() { return mock_context_.mock_executor(); }

  void AddEventObserver(mojo::PendingRemote<mojom::EventObserver> observer) {
    stylus_events_impl_.AddObserver(std::move(observer));
  }

  void EmitStylusConnectedEvent(const mojom::StylusConnectedEventPtr& event) {
    stylus_observer_->OnConnected(event.Clone());
  }
  void EmitStylusTouchEvent(const mojom::StylusTouchEventPtr& event) {
    stylus_observer_->OnTouch(event.Clone());
  }

  mojo::Remote<mojom::StylusObserver> stylus_observer_;
  FakeProcessControl process_control_;

 private:
  base::test::TaskEnvironment task_environment_;
  MockContext mock_context_;
  StylusEventsImpl stylus_events_impl_{&mock_context_};
};

// Test that we can receive stylus touch events.
TEST_F(StylusEventsImplTest, StylusTouchEvent) {
  mojom::StylusTouchEvent fake_touch_event;
  fake_touch_event.touch_point = mojom::StylusTouchPointInfo::New();

  EventObserverTestFuture event_observer;
  AddEventObserver(event_observer.BindNewPendingRemote());

  EmitStylusTouchEvent(fake_touch_event.Clone());

  auto info = event_observer.WaitForEvent();
  ASSERT_TRUE(info->is_stylus_event_info());
  const auto& stylus_event_info = info->get_stylus_event_info();
  ASSERT_TRUE(stylus_event_info->is_touch_event());
  EXPECT_EQ(fake_touch_event, *stylus_event_info->get_touch_event());
}

// Test that we can receive stylus connected events.
TEST_F(StylusEventsImplTest, StylusConnectedEvent) {
  mojom::StylusConnectedEvent fake_connected_event;
  fake_connected_event.max_x = 1;
  fake_connected_event.max_y = 2;

  EventObserverTestFuture event_observer;
  AddEventObserver(event_observer.BindNewPendingRemote());

  EmitStylusConnectedEvent(fake_connected_event.Clone());

  auto info = event_observer.WaitForEvent();
  ASSERT_TRUE(info->is_stylus_event_info());
  const auto& stylus_event_info = info->get_stylus_event_info();
  ASSERT_TRUE(stylus_event_info->is_connected_event());
  EXPECT_EQ(fake_connected_event, *stylus_event_info->get_connected_event());
}

// Test that we can receive stylus connected events by multiple observers.
TEST_F(StylusEventsImplTest, StylusConnectedEventWithMultipleObservers) {
  mojom::StylusConnectedEvent fake_connected_event;
  fake_connected_event.max_x = 1;
  fake_connected_event.max_y = 2;

  EventObserverTestFuture event_observer, event_observer2;
  AddEventObserver(event_observer.BindNewPendingRemote());
  AddEventObserver(event_observer2.BindNewPendingRemote());

  EmitStylusConnectedEvent(fake_connected_event.Clone());

  auto check_result = [&fake_connected_event](mojom::EventInfoPtr info) {
    ASSERT_TRUE(info->is_stylus_event_info());
    const auto& stylus_event_info = info->get_stylus_event_info();
    ASSERT_TRUE(stylus_event_info->is_connected_event());
    EXPECT_EQ(fake_connected_event, *stylus_event_info->get_connected_event());
  };

  check_result(event_observer.WaitForEvent());
  check_result(event_observer2.WaitForEvent());
}

// Test that process control is reset when delegate observer disconnects.
TEST_F(StylusEventsImplTest,
       ProcessControlResetWhenDelegateObserverDisconnects) {
  EventObserverTestFuture event_observer;
  AddEventObserver(event_observer.BindNewPendingRemote());

  process_control_.receiver().FlushForTesting();
  EXPECT_TRUE(process_control_.IsConnected());

  // Simulate the disconnection of delegate observer.
  stylus_observer_.FlushForTesting();
  stylus_observer_.reset();

  process_control_.receiver().FlushForTesting();
  EXPECT_FALSE(process_control_.IsConnected());
}

// Test that process control is reset when there is no event observer.
TEST_F(StylusEventsImplTest, ProcessControlResetWhenNoEventObserver) {
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
