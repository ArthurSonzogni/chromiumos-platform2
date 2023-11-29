// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/events/stylus_garage_events_impl.h"

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

// Tests for the StylusGarageEventsImpl class.
class StylusGarageEventsImplTest : public testing::Test {
 public:
  StylusGarageEventsImplTest(const StylusGarageEventsImplTest&) = delete;
  StylusGarageEventsImplTest& operator=(const StylusGarageEventsImplTest&) =
      delete;

 protected:
  StylusGarageEventsImplTest() = default;

  void SetUp() override {
    EXPECT_CALL(*mock_executor(), MonitorStylusGarage(_, _))
        .WillOnce([=, this](auto stylus_garage_observer,
                            auto pending_process_control) {
          stylus_garage_observer_.Bind(std::move(stylus_garage_observer));
          process_control_.BindReceiver(std::move(pending_process_control));
        });
    stylus_garage_events_impl_.AddObserver(
        event_observer_.BindNewPendingRemote());
  }

  EventObserverTestFuture& event_observer() { return event_observer_; }
  MockExecutor* mock_executor() { return mock_context_.mock_executor(); }

  void EmitStylusGarageInsertEvent() { stylus_garage_observer_->OnInsert(); }

  void EmitStylusGarageRemoveEvent() { stylus_garage_observer_->OnRemove(); }

  mojo::Remote<mojom::StylusGarageObserver> stylus_garage_observer_;
  FakeProcessControl process_control_;

 private:
  base::test::TaskEnvironment task_environment_;
  MockContext mock_context_;
  EventObserverTestFuture event_observer_;
  StylusGarageEventsImpl stylus_garage_events_impl_{&mock_context_};
};

// Test that we can receive stylus garage insert events.
TEST_F(StylusGarageEventsImplTest, StylusGarageInsertEvent) {
  EmitStylusGarageInsertEvent();

  auto info = event_observer().WaitForEvent();
  ASSERT_TRUE(info->is_stylus_garage_event_info());
  const auto& stylus_garage_event_info = info->get_stylus_garage_event_info();
  EXPECT_EQ(stylus_garage_event_info->state,
            mojom::StylusGarageEventInfo::State::kInserted);
}

// Test that we can receive stylus garage remove events.
TEST_F(StylusGarageEventsImplTest, StylusGarageRemoveEvent) {
  EmitStylusGarageRemoveEvent();

  auto info = event_observer().WaitForEvent();
  ASSERT_TRUE(info->is_stylus_garage_event_info());
  const auto& stylus_garage_event_info = info->get_stylus_garage_event_info();
  EXPECT_EQ(stylus_garage_event_info->state,
            mojom::StylusGarageEventInfo::State::kRemoved);
}

// Test that process control is reset when delegate observer disconnects.
TEST_F(StylusGarageEventsImplTest,
       ProcessControlResetWhenDelegateObserverDisconnects) {
  process_control_.receiver().FlushForTesting();
  EXPECT_TRUE(process_control_.IsConnected());

  // Simulate the disconnection of delegate observer.
  stylus_garage_observer_.FlushForTesting();
  stylus_garage_observer_.reset();

  process_control_.receiver().FlushForTesting();
  EXPECT_FALSE(process_control_.IsConnected());
}

// Test that process control is reset when there is no event observer.
TEST_F(StylusGarageEventsImplTest, ProcessControlResetWhenNoEventObserver) {
  process_control_.receiver().FlushForTesting();
  EXPECT_TRUE(process_control_.IsConnected());

  event_observer().Reset();

  process_control_.receiver().FlushForTesting();
  EXPECT_FALSE(process_control_.IsConnected());
}

}  // namespace
}  // namespace diagnostics
