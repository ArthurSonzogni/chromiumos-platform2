// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include <base/check.h>
#include <base/run_loop.h>
#include <base/test/task_environment.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <power_manager/dbus-proxy-mocks.h>

#include "diagnostics/cros_healthd/events/lid_events_impl.h"
#include "diagnostics/cros_healthd/events/mock_event_observer.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "diagnostics/mojom/public/cros_healthd_events.mojom.h"

namespace diagnostics {
namespace {

namespace mojom = ::ash::cros_healthd::mojom;

using ::testing::_;
using ::testing::Invoke;
using ::testing::SaveArg;
using ::testing::StrictMock;

// Tests for the LidEventsImpl class.
class LidEventsImplTest : public testing::Test {
 protected:
  LidEventsImplTest() = default;
  LidEventsImplTest(const LidEventsImplTest&) = delete;
  LidEventsImplTest& operator=(const LidEventsImplTest&) = delete;

  void SetUp() override {
    EXPECT_CALL(*mock_power_manager_proxy(),
                DoRegisterLidClosedSignalHandler(_, _))
        .WillOnce(SaveArg<0>(&lid_closed_callback_));
    EXPECT_CALL(*mock_power_manager_proxy(),
                DoRegisterLidOpenedSignalHandler(_, _))
        .WillOnce(SaveArg<0>(&lid_opened_callback_));
    lid_events_impl_ = std::make_unique<LidEventsImpl>(&mock_context_);

    mojo::PendingRemote<mojom::EventObserver> observer;
    mojo::PendingReceiver<mojom::EventObserver> observer_receiver(
        observer.InitWithNewPipeAndPassReceiver());
    observer_ = std::make_unique<StrictMock<MockEventObserver>>(
        std::move(observer_receiver));
    lid_events_impl_->AddObserver(std::move(observer));
  }

  org::chromium::PowerManagerProxyMock* mock_power_manager_proxy() {
    return mock_context_.mock_power_manager_proxy();
  }

  MockEventObserver* mock_observer() { return observer_.get(); }

  void EmitLidClosedSignal() { lid_closed_callback_.Run(); }

  void EmitLidOpenedSignal() { lid_opened_callback_.Run(); }

 private:
  base::test::TaskEnvironment task_environment_;
  MockContext mock_context_;
  std::unique_ptr<StrictMock<MockEventObserver>> observer_;
  std::unique_ptr<LidEventsImpl> lid_events_impl_;
  base::RepeatingClosure lid_closed_callback_;
  base::RepeatingClosure lid_opened_callback_;
};

// Test that we can receive lid closed events.
TEST_F(LidEventsImplTest, ReceiveLidClosedEvent) {
  base::RunLoop run_loop;
  EXPECT_CALL(*mock_observer(), OnEvent(_))
      .WillOnce(Invoke([&](mojom::EventInfoPtr info) {
        EXPECT_TRUE(info->is_lid_event_info());
        const auto& lid_event_info = info->get_lid_event_info();
        EXPECT_EQ(lid_event_info->state, mojom::LidEventInfo::State::kClosed);
        run_loop.Quit();
      }));

  EmitLidClosedSignal();

  run_loop.Run();
}

// Test that we can receive lid opened events.
TEST_F(LidEventsImplTest, ReceiveLidOpenedEvent) {
  base::RunLoop run_loop;
  EXPECT_CALL(*mock_observer(), OnEvent(_))
      .WillOnce(Invoke([&](mojom::EventInfoPtr info) {
        EXPECT_TRUE(info->is_lid_event_info());
        const auto& lid_event_info = info->get_lid_event_info();
        EXPECT_EQ(lid_event_info->state, mojom::LidEventInfo::State::kOpened);
        run_loop.Quit();
      }));

  EmitLidOpenedSignal();

  run_loop.Run();
}

}  // namespace
}  // namespace diagnostics
