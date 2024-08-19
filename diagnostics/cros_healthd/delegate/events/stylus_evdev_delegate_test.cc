// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/delegate/events/stylus_evdev_delegate.h"

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

class MockStylusObserver : public mojom::StylusObserver {
 public:
  // ash::cros_healthd::mojom::StylusObserver overrides:
  MOCK_METHOD(void, OnConnected, (mojom::StylusConnectedEventPtr), (override));
  MOCK_METHOD(void, OnTouch, (mojom::StylusTouchEventPtr), (override));
};

class StylusEvdevDelegateTest : public ::testing::Test {
 protected:
  base::test::TaskEnvironment task_environment_;
  MockStylusObserver mock_observer_;
  mojo::Receiver<mojom::StylusObserver> receiver_{&mock_observer_};
  StylusEvdevDelegate delegate_{receiver_.BindNewPipeAndPassRemote()};
};

TEST_F(StylusEvdevDelegateTest, IsTarget) {
  test::MockLibevdevWrapper dev;
  ON_CALL(dev, HasProperty(INPUT_PROP_POINTER)).WillByDefault(Return(false));
  ON_CALL(dev, HasProperty(INPUT_PROP_DIRECT)).WillByDefault(Return(true));
  ON_CALL(dev, HasEventCode(EV_ABS, ABS_MT_TRACKING_ID))
      .WillByDefault(Return(false));
  ON_CALL(dev, HasEventCode(EV_KEY, BTN_TOOL_PEN)).WillByDefault(Return(true));

  EXPECT_TRUE(delegate_.IsTarget(&dev));
}

TEST_F(StylusEvdevDelegateTest, IsNotTargetForTouchscreen) {
  test::MockLibevdevWrapper dev;
  ON_CALL(dev, HasProperty(INPUT_PROP_POINTER)).WillByDefault(Return(false));
  ON_CALL(dev, HasProperty(INPUT_PROP_DIRECT)).WillByDefault(Return(true));
  ON_CALL(dev, HasEventCode(EV_ABS, ABS_MT_TRACKING_ID))
      .WillByDefault(Return(true));

  EXPECT_FALSE(delegate_.IsTarget(&dev));
}

TEST_F(StylusEvdevDelegateTest, FireOnTouchEvent) {
  test::MockLibevdevWrapper dev;
  ON_CALL(dev, GetNumSlots()).WillByDefault(Return(1));
  ON_CALL(dev, GetEventValue(EV_ABS, ABS_X)).WillByDefault(DoAll(Return(20)));
  ON_CALL(dev, GetEventValue(EV_ABS, ABS_Y)).WillByDefault(DoAll(Return(30)));
  ON_CALL(dev, GetEventValue(EV_ABS, ABS_PRESSURE))
      .WillByDefault(DoAll(Return(40)));
  ON_CALL(dev, GetEventValue(EV_KEY, BTN_TOUCH))
      .WillByDefault(DoAll(Return(1)));

  input_event ev = {.type = EV_SYN, .code = SYN_REPORT};

  mojom::StylusTouchEventPtr received_event;
  EXPECT_CALL(mock_observer_, OnTouch(_))
      .WillRepeatedly(
          [&received_event](auto event) { received_event = std::move(event); });

  delegate_.FireEvent(ev, &dev);
  receiver_.FlushForTesting();

  ASSERT_TRUE(received_event);
  const auto& touch_point = received_event->touch_point;
  ASSERT_TRUE(touch_point);
  EXPECT_EQ(touch_point->x, 20);
  EXPECT_EQ(touch_point->y, 30);
  EXPECT_EQ(touch_point->pressure, mojom::NullableUint32::New(40));

  // The stylus leaves the contact.
  ON_CALL(dev, GetEventValue(EV_KEY, BTN_TOUCH))
      .WillByDefault(DoAll(Return(0)));

  delegate_.FireEvent(ev, &dev);
  receiver_.FlushForTesting();

  ASSERT_TRUE(received_event);
  EXPECT_FALSE(received_event->touch_point);
}

TEST_F(StylusEvdevDelegateTest, InitializationFail) {
  base::test::TestFuture<uint32_t, const std::string&> future;
  receiver_.set_disconnect_with_reason_handler(future.GetCallback());

  delegate_.InitializationFail(/*custom_reason=*/42,
                               /*description=*/"error description");

  EXPECT_EQ(future.Get<uint32_t>(), 42);
  EXPECT_EQ(future.Get<std::string>(), "error description");
}

TEST_F(StylusEvdevDelegateTest, ReportProperties) {
  test::MockLibevdevWrapper dev;
  ON_CALL(dev, GetAbsMaximum(ABS_X)).WillByDefault(Return(300));
  ON_CALL(dev, GetAbsMaximum(ABS_Y)).WillByDefault(Return(150));
  ON_CALL(dev, GetAbsMaximum(ABS_PRESSURE)).WillByDefault(Return(64));

  mojom::StylusConnectedEventPtr received_event;

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
