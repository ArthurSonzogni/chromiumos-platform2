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
#include "diagnostics/cros_healthd/events/stylus_garage_events_impl.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "diagnostics/mojom/public/cros_healthd_events.mojom.h"

namespace diagnostics {
namespace {

namespace mojom = ::ash::cros_healthd::mojom;

using ::testing::_;
using ::testing::Invoke;
using ::testing::StrictMock;
using ::testing::WithArg;

// Tests for the StylusGarageEventsImpl class.
class StylusGarageEventsImplTest : public testing::Test {
 protected:
  StylusGarageEventsImplTest() = default;
  StylusGarageEventsImplTest(const StylusGarageEventsImplTest&) = delete;
  StylusGarageEventsImplTest& operator=(const StylusGarageEventsImplTest&) =
      delete;

  void SetUp() override {
    stylus_garage_events_impl_ =
        std::make_unique<StylusGarageEventsImpl>(&mock_context_);

    mojo::PendingRemote<mojom::EventObserver> stylus_garage_observer;
    mojo::PendingReceiver<mojom::EventObserver> observer_receiver(
        stylus_garage_observer.InitWithNewPipeAndPassReceiver());
    event_observer_ = std::make_unique<StrictMock<MockEventObserver>>(
        std::move(observer_receiver));
    SetExecutorMonitorStylusGarage();
    stylus_garage_events_impl_->AddObserver(std::move(stylus_garage_observer));
  }

  MockEventObserver* mock_event_observer() { return event_observer_.get(); }
  MockExecutor* mock_executor() { return mock_context_.mock_executor(); }

  void SetExecutorMonitorStylusGarage() {
    EXPECT_CALL(*mock_executor(), MonitorStylusGarage(_, _))
        .WillOnce(
            WithArg<0>([=](mojo::PendingRemote<mojom::StylusGarageObserver>
                               stylus_garage_observer) {
              stylus_garage_observer_.Bind(std::move(stylus_garage_observer));
            }));
  }

  void EmitStylusGarageInsertEvent() { stylus_garage_observer_->OnInsert(); }

  void EmitStylusGarageRemoveEvent() { stylus_garage_observer_->OnRemove(); }

 private:
  base::test::TaskEnvironment task_environment_;
  MockContext mock_context_;
  std::unique_ptr<StrictMock<MockEventObserver>> event_observer_;
  std::unique_ptr<StylusGarageEventsImpl> stylus_garage_events_impl_;
  mojo::Remote<mojom::StylusGarageObserver> stylus_garage_observer_;
};

// Test that we can receive stylus garage insert events.
TEST_F(StylusGarageEventsImplTest, StylusGarageInsertEvent) {
  base::RunLoop run_loop;
  EXPECT_CALL(*mock_event_observer(), OnEvent(_))
      .WillOnce(Invoke([&](mojom::EventInfoPtr info) {
        EXPECT_TRUE(info->is_stylus_garage_event_info());
        const auto& stylus_garage_event_info =
            info->get_stylus_garage_event_info();
        EXPECT_EQ(stylus_garage_event_info->state,
                  mojom::StylusGarageEventInfo::State::kInsert);
        run_loop.Quit();
      }));

  EmitStylusGarageInsertEvent();

  run_loop.Run();
}

// Test that we can receive stylus garage remove events.
TEST_F(StylusGarageEventsImplTest, StylusGarageRemoveEvent) {
  base::RunLoop run_loop;
  EXPECT_CALL(*mock_event_observer(), OnEvent(_))
      .WillOnce(Invoke([&](mojom::EventInfoPtr info) {
        EXPECT_TRUE(info->is_stylus_garage_event_info());
        const auto& stylus_garage_event_info =
            info->get_stylus_garage_event_info();
        EXPECT_EQ(stylus_garage_event_info->state,
                  mojom::StylusGarageEventInfo::State::kRemove);
        run_loop.Quit();
      }));

  EmitStylusGarageRemoveEvent();

  run_loop.Run();
}

}  // namespace
}  // namespace diagnostics
