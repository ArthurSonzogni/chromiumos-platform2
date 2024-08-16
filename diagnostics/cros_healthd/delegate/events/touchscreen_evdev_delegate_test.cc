// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/delegate/events/touchscreen_evdev_delegate.h"

#include <linux/input-event-codes.h>
#include <linux/input.h>

#include <string>
#include <utility>

#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <mojo/public/cpp/bindings/receiver.h>

#include "diagnostics/cros_healthd/delegate/utils/test/mock_libevdev_wrapper.h"
#include "diagnostics/cros_healthd/mojom/executor.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_events.mojom.h"

namespace diagnostics {
namespace {

namespace mojom = ::ash::cros_healthd::mojom;

using ::testing::_;
using ::testing::DoAll;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::SizeIs;
using ::testing::UnorderedElementsAre;

class MockTouchscreenObserver : public mojom::TouchscreenObserver {
 public:
  // ash::cros_healthd::mojom::TouchscreenObserver overrides:
  MOCK_METHOD(void,
              OnConnected,
              (mojom::TouchscreenConnectedEventPtr),
              (override));
  MOCK_METHOD(void, OnTouch, (mojom::TouchscreenTouchEventPtr), (override));
};

class TouchscreenEvdevDelegateTest : public ::testing::Test {
 protected:
  base::test::TaskEnvironment task_environment_;
  MockTouchscreenObserver mock_observer_;
  mojo::Receiver<mojom::TouchscreenObserver> receiver_{&mock_observer_};
  TouchscreenEvdevDelegate delegate_{receiver_.BindNewPipeAndPassRemote()};
};

TEST_F(TouchscreenEvdevDelegateTest, IsTarget) {
  test::MockLibevdevWrapper dev;
  ON_CALL(dev, HasProperty(INPUT_PROP_POINTER)).WillByDefault(Return(false));
  ON_CALL(dev, HasProperty(INPUT_PROP_DIRECT)).WillByDefault(Return(true));
  ON_CALL(dev, HasEventCode(EV_ABS, ABS_MT_TRACKING_ID))
      .WillByDefault(Return(true));

  EXPECT_TRUE(delegate_.IsTarget(&dev));
}

TEST_F(TouchscreenEvdevDelegateTest, IsNotTargetForStylus) {
  test::MockLibevdevWrapper dev;
  ON_CALL(dev, HasProperty(INPUT_PROP_POINTER)).WillByDefault(Return(false));
  ON_CALL(dev, HasProperty(INPUT_PROP_DIRECT)).WillByDefault(Return(true));
  ON_CALL(dev, HasEventCode(EV_ABS, ABS_MT_TRACKING_ID))
      .WillByDefault(Return(false));

  EXPECT_FALSE(delegate_.IsTarget(&dev));
}

TEST_F(TouchscreenEvdevDelegateTest, FireOnTouchEvent) {
  test::MockLibevdevWrapper dev;
  ON_CALL(dev, GetNumSlots()).WillByDefault(Return(1));
  ON_CALL(dev, FetchSlotValue(0, ABS_MT_TRACKING_ID, _))
      .WillByDefault(DoAll(SetArgPointee<2>(0), Return(1)));
  ON_CALL(dev, FetchSlotValue(0, ABS_MT_POSITION_X, _))
      .WillByDefault(DoAll(SetArgPointee<2>(20), Return(1)));
  ON_CALL(dev, FetchSlotValue(0, ABS_MT_POSITION_Y, _))
      .WillByDefault(DoAll(SetArgPointee<2>(30), Return(1)));

  input_event ev = {.type = EV_SYN, .code = SYN_REPORT};

  mojom::TouchscreenTouchEventPtr received_event;
  EXPECT_CALL(mock_observer_, OnTouch(_))
      .WillOnce(
          [&received_event](auto event) { received_event = std::move(event); });

  delegate_.FireEvent(ev, &dev);
  receiver_.FlushForTesting();

  ASSERT_TRUE(received_event);
  ASSERT_THAT(received_event->touch_points, SizeIs(1));
  const auto& touch_point = received_event->touch_points[0];
  EXPECT_EQ(touch_point->tracking_id, 0);
  EXPECT_EQ(touch_point->x, 20);
  EXPECT_EQ(touch_point->y, 30);
}

TEST_F(TouchscreenEvdevDelegateTest, InitializationFail) {
  base::test::TestFuture<uint32_t, const std::string&> future;
  receiver_.set_disconnect_with_reason_handler(future.GetCallback());

  delegate_.InitializationFail(/*custom_reason=*/42,
                               /*description=*/"error description");

  EXPECT_EQ(future.Get<uint32_t>(), 42);
  EXPECT_EQ(future.Get<std::string>(), "error description");
}

TEST_F(TouchscreenEvdevDelegateTest, ReportProperties) {
  test::MockLibevdevWrapper dev;
  ON_CALL(dev, GetAbsMaximum(ABS_X)).WillByDefault(Return(300));
  ON_CALL(dev, GetAbsMaximum(ABS_Y)).WillByDefault(Return(150));
  ON_CALL(dev, GetAbsMaximum(ABS_MT_PRESSURE)).WillByDefault(Return(64));

  mojom::TouchscreenConnectedEventPtr received_event;

  EXPECT_CALL(mock_observer_, OnConnected(_))
      .WillOnce(
          [&received_event](auto event) { received_event = std::move(event); });

  delegate_.ReportProperties(&dev);
  receiver_.FlushForTesting();

  ASSERT_TRUE(received_event);
  EXPECT_EQ(received_event->max_x, 300);
  EXPECT_EQ(received_event->max_y, 150);
  EXPECT_EQ(received_event->max_pressure, 64);
}

}  // namespace
}  // namespace diagnostics
