// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/events/touchpad_events_impl.h"

#include <memory>
#include <utility>

#include <base/check.h>
#include <base/test/gmock_callback_support.h>
#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <mojo/public/cpp/bindings/pending_remote.h>

#include "diagnostics/cros_healthd/events/mock_event_observer.h"
#include "diagnostics/cros_healthd/executor/utils/fake_process_control.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "diagnostics/cros_healthd/utils/mojo_test_utils.h"
#include "diagnostics/mojom/public/cros_healthd_events.mojom.h"

namespace diagnostics {
namespace {

namespace mojom = ::ash::cros_healthd::mojom;

using ::testing::_;
using ::testing::DoAll;
using ::testing::StrictMock;

// Tests for the TouchpadEventsImpl class.
class TouchpadEventsImplTest : public testing::Test {
 public:
  TouchpadEventsImplTest(const TouchpadEventsImplTest&) = delete;
  TouchpadEventsImplTest& operator=(const TouchpadEventsImplTest&) = delete;

 protected:
  TouchpadEventsImplTest() = default;

  void SetUp() override {
    touchpad_events_impl_ =
        std::make_unique<TouchpadEventsImpl>(&mock_context_);

    SetExecutorMonitorTouchpad();
    event_observer_ = CreateAndAddMockObserver();
  }

  MockEventObserver* mock_event_observer() { return event_observer_.get(); }
  MockExecutor* mock_executor() { return mock_context_.mock_executor(); }

  std::unique_ptr<StrictMock<MockEventObserver>> CreateAndAddMockObserver() {
    mojo::PendingRemote<mojom::EventObserver> touchpad_observer;
    mojo::PendingReceiver<mojom::EventObserver> observer_receiver(
        touchpad_observer.InitWithNewPipeAndPassReceiver());
    touchpad_events_impl_->AddObserver(std::move(touchpad_observer));
    return std::make_unique<StrictMock<MockEventObserver>>(
        std::move(observer_receiver));
  }

  void SetExecutorMonitorTouchpad() {
    EXPECT_CALL(*mock_executor(), MonitorTouchpad(_, _))
        .WillOnce(
            [=, this](auto touchpad_observer, auto pending_process_control) {
              touchpad_observer_.Bind(std::move(touchpad_observer));
              process_control_.BindReceiver(std::move(pending_process_control));
            });
  }

  void ResetEventObserver() { event_observer_.reset(); }

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
  std::unique_ptr<StrictMock<MockEventObserver>> event_observer_;
  std::unique_ptr<TouchpadEventsImpl> touchpad_events_impl_;
};

// Test that we can receive touchpad touch events.
TEST_F(TouchpadEventsImplTest, TouchpadTouchEvent) {
  auto fake_touch_event = mojom::TouchpadTouchEvent::New();
  fake_touch_event->touch_points.push_back(mojom::TouchPointInfo::New());

  base::test::TestFuture<void> future;
  mojom::EventInfoPtr recv_info;
  EXPECT_CALL(*mock_event_observer(), OnEvent(_))
      .WillOnce(DoAll(SaveMojomArg<0>(&recv_info),
                      base::test::RunOnceClosure(future.GetCallback())));

  EmitTouchpadTouchEvent(fake_touch_event);

  EXPECT_TRUE(future.Wait());
  EXPECT_TRUE(recv_info->is_touchpad_event_info());
  const auto& touchpad_event_info = recv_info->get_touchpad_event_info();
  EXPECT_TRUE(touchpad_event_info->is_touch_event());
  EXPECT_EQ(fake_touch_event, touchpad_event_info->get_touch_event());
}

// Test that we can receive touchpad button events.
TEST_F(TouchpadEventsImplTest, TouchpadButtonEvent) {
  auto fake_button_event = mojom::TouchpadButtonEvent::New();
  fake_button_event->button = mojom::InputTouchButton::kLeft;
  fake_button_event->pressed = true;

  base::test::TestFuture<void> future;
  mojom::EventInfoPtr recv_info;
  EXPECT_CALL(*mock_event_observer(), OnEvent(_))
      .WillOnce(DoAll(SaveMojomArg<0>(&recv_info),
                      base::test::RunOnceClosure(future.GetCallback())));

  EmitTouchpadButtonEvent(fake_button_event);

  EXPECT_TRUE(future.Wait());
  EXPECT_TRUE(recv_info->is_touchpad_event_info());
  const auto& touchpad_event_info = recv_info->get_touchpad_event_info();
  EXPECT_TRUE(touchpad_event_info->is_button_event());
  EXPECT_EQ(fake_button_event, touchpad_event_info->get_button_event());
}

// Test that we can receive touchpad connected events.
TEST_F(TouchpadEventsImplTest, TouchpadConnectedEvent) {
  auto fake_connected_event = mojom::TouchpadConnectedEvent::New();
  fake_connected_event->max_x = 1;
  fake_connected_event->max_y = 2;
  fake_connected_event->buttons = {mojom::InputTouchButton::kLeft};

  base::test::TestFuture<void> future;
  mojom::EventInfoPtr recv_info;
  EXPECT_CALL(*mock_event_observer(), OnEvent(_))
      .WillOnce(DoAll(SaveMojomArg<0>(&recv_info),
                      base::test::RunOnceClosure(future.GetCallback())));

  EmitTouchpadConnectedEvent(fake_connected_event);

  EXPECT_TRUE(future.Wait());
  EXPECT_TRUE(recv_info->is_touchpad_event_info());
  const auto& touchpad_event_info = recv_info->get_touchpad_event_info();
  EXPECT_TRUE(touchpad_event_info->is_connected_event());
  EXPECT_EQ(fake_connected_event, touchpad_event_info->get_connected_event());
}

// Test that we can receive touchpad connected events by multiple observers.
TEST_F(TouchpadEventsImplTest, TouchpadConnectedEventWithMultipleObservers) {
  auto fake_connected_event = mojom::TouchpadConnectedEvent::New();
  fake_connected_event->max_x = 1;
  fake_connected_event->max_y = 2;
  fake_connected_event->buttons = {mojom::InputTouchButton::kLeft};

  auto second_event_observer = CreateAndAddMockObserver();

  base::test::TestFuture<void> future;
  mojom::EventInfoPtr recv_info_1, recv_info_2;
  EXPECT_CALL(*mock_event_observer(), OnEvent(_))
      .WillOnce(DoAll(SaveMojomArg<0>(&recv_info_1),
                      SaveMojomArg<0>(&recv_info_2),
                      base::test::RunOnceClosure(future.GetCallback())));

  EmitTouchpadConnectedEvent(fake_connected_event);

  EXPECT_TRUE(future.Wait());
  ASSERT_TRUE(recv_info_1->is_touchpad_event_info());
  const auto& touchpad_event_info_1 = recv_info_1->get_touchpad_event_info();
  ASSERT_TRUE(touchpad_event_info_1->is_connected_event());
  EXPECT_EQ(fake_connected_event, touchpad_event_info_1->get_connected_event());

  ASSERT_TRUE(recv_info_2->is_touchpad_event_info());
  const auto& touchpad_event_info_2 = recv_info_2->get_touchpad_event_info();
  ASSERT_TRUE(touchpad_event_info_2->is_connected_event());
  EXPECT_EQ(fake_connected_event, touchpad_event_info_2->get_connected_event());
}

// Test that process control is reset when delegate observer disconnects.
TEST_F(TouchpadEventsImplTest,
       ProcessControlResetWhenDelegateObserverDisconnects) {
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
  process_control_.receiver().FlushForTesting();
  EXPECT_TRUE(process_control_.IsConnected());

  ResetEventObserver();

  process_control_.receiver().FlushForTesting();
  EXPECT_FALSE(process_control_.IsConnected());
}

}  // namespace
}  // namespace diagnostics
