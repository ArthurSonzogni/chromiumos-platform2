// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include <base/check.h>
#include <base/run_loop.h>
#include <base/test/task_environment.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <mojo/public/cpp/bindings/pending_remote.h>

#include "diagnostics/cros_healthd/events/mock_event_observer.h"
#include "diagnostics/cros_healthd/events/stylus_events_impl.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "diagnostics/mojom/public/cros_healthd_events.mojom.h"

namespace diagnostics {
namespace {

namespace mojom = ::ash::cros_healthd::mojom;

using ::testing::_;
using ::testing::Invoke;
using ::testing::StrictMock;
using ::testing::WithArg;

// Tests for the StylusEventsImpl class.
class StylusEventsImplTest : public testing::Test {
 protected:
  StylusEventsImplTest() = default;
  StylusEventsImplTest(const StylusEventsImplTest&) = delete;
  StylusEventsImplTest& operator=(const StylusEventsImplTest&) = delete;

  void SetUp() override {
    stylus_events_impl_ = std::make_unique<StylusEventsImpl>(&mock_context_);

    SetExecutorMonitorStylus();
    event_observer_ = CreateAndAddMockObserver();
  }

  MockEventObserver* mock_event_observer() { return event_observer_.get(); }
  MockExecutor* mock_executor() { return mock_context_.mock_executor(); }

  std::unique_ptr<StrictMock<MockEventObserver>> CreateAndAddMockObserver() {
    mojo::PendingRemote<mojom::EventObserver> stylus_observer;
    mojo::PendingReceiver<mojom::EventObserver> observer_receiver(
        stylus_observer.InitWithNewPipeAndPassReceiver());
    stylus_events_impl_->AddObserver(std::move(stylus_observer));
    return std::make_unique<StrictMock<MockEventObserver>>(
        std::move(observer_receiver));
  }

  void SetExecutorMonitorStylus() {
    EXPECT_CALL(*mock_executor(), MonitorStylus(_, _))
        .WillOnce(WithArg<0>(
            [=](mojo::PendingRemote<mojom::StylusObserver> stylus_observer) {
              stylus_observer_.Bind(std::move(stylus_observer));
            }));
  }

  void EmitStylusConnectedEvent(const mojom::StylusConnectedEventPtr& event) {
    stylus_observer_->OnConnected(event.Clone());
  }
  void EmitStylusTouchEvent(const mojom::StylusTouchEventPtr& event) {
    stylus_observer_->OnTouch(event.Clone());
  }

 private:
  base::test::TaskEnvironment task_environment_;
  MockContext mock_context_;
  std::unique_ptr<StrictMock<MockEventObserver>> event_observer_;
  std::unique_ptr<StylusEventsImpl> stylus_events_impl_;
  mojo::Remote<mojom::StylusObserver> stylus_observer_;
};

// Test that we can receive stylus touch events.
TEST_F(StylusEventsImplTest, StylusTouchEvent) {
  mojom::StylusTouchEvent fake_touch_event;
  fake_touch_event.touch_point = mojom::StylusTouchPointInfo::New();

  base::RunLoop run_loop;
  EXPECT_CALL(*mock_event_observer(), OnEvent(_))
      .WillOnce(Invoke([&](mojom::EventInfoPtr info) {
        EXPECT_TRUE(info->is_stylus_event_info());
        const auto& stylus_event_info = info->get_stylus_event_info();
        EXPECT_TRUE(stylus_event_info->is_touch_event());
        EXPECT_EQ(fake_touch_event, *stylus_event_info->get_touch_event());
        run_loop.Quit();
      }));

  EmitStylusTouchEvent(fake_touch_event.Clone());

  run_loop.Run();
}

// Test that we can receive stylus connected events.
TEST_F(StylusEventsImplTest, StylusConnectedEvent) {
  mojom::StylusConnectedEvent fake_connected_event;
  fake_connected_event.max_x = 1;
  fake_connected_event.max_y = 2;

  base::RunLoop run_loop;
  EXPECT_CALL(*mock_event_observer(), OnEvent(_))
      .WillOnce(Invoke([&](mojom::EventInfoPtr info) {
        EXPECT_TRUE(info->is_stylus_event_info());
        const auto& stylus_event_info = info->get_stylus_event_info();
        EXPECT_TRUE(stylus_event_info->is_connected_event());
        EXPECT_EQ(fake_connected_event,
                  *stylus_event_info->get_connected_event());
        run_loop.Quit();
      }));

  EmitStylusConnectedEvent(fake_connected_event.Clone());

  run_loop.Run();
}

// Test that we can receive stylus connected events by multiple observers.
TEST_F(StylusEventsImplTest, StylusConnectedEventWithMultipleObservers) {
  mojom::StylusConnectedEvent fake_connected_event;
  fake_connected_event.max_x = 1;
  fake_connected_event.max_y = 2;

  auto second_event_observer = CreateAndAddMockObserver();

  int counter = 2;
  base::RunLoop run_loop;
  auto on_event = [&](mojom::EventInfoPtr info) {
    EXPECT_TRUE(info->is_stylus_event_info());
    const auto& stylus_event_info = info->get_stylus_event_info();
    EXPECT_TRUE(stylus_event_info->is_connected_event());
    EXPECT_EQ(fake_connected_event, *stylus_event_info->get_connected_event());
    counter--;
    if (counter == 0) {
      run_loop.Quit();
    }
  };
  EXPECT_CALL(*mock_event_observer(), OnEvent(_)).WillOnce(Invoke(on_event));
  EXPECT_CALL(*second_event_observer, OnEvent(_)).WillOnce(Invoke(on_event));

  EmitStylusConnectedEvent(fake_connected_event.Clone());

  run_loop.Run();
}

}  // namespace
}  // namespace diagnostics
