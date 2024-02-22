// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/delegate/events/power_button_evdev_delegate.h"

#include <linux/input.h>
#include <linux/input-event-codes.h>

#include <string>

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
using ::testing::Assign;
using ::testing::Return;
using ::testing::SaveArg;

class MockPowerButtonObserver : public mojom::PowerButtonObserver {
 public:
  // ash::cros_healthd::mojom::PowerButtonObserver overrides:
  MOCK_METHOD(void,
              OnEvent,
              (mojom::PowerButtonObserver::ButtonState button_state),
              (override));
  MOCK_METHOD(void, OnConnectedToEventNode, (), (override));
};

class PowerButtonEvdevDelegateTest : public ::testing::Test {
 protected:
  base::test::TaskEnvironment task_environment_;
  MockPowerButtonObserver mock_observer_;
  mojo::Receiver<mojom::PowerButtonObserver> receiver_{&mock_observer_};
  PowerButtonEvdevDelegate delegate_{receiver_.BindNewPipeAndPassRemote()};
};

TEST_F(PowerButtonEvdevDelegateTest, IsTarget) {
  test::MockLibevdevWrapper dev;
  ON_CALL(dev, HasEventCode(EV_KEY, KEY_POWER)).WillByDefault(Return(true));
  ON_CALL(dev, GetIdBustype()).WillByDefault(Return(BUS_HOST));

  EXPECT_TRUE(delegate_.IsTarget(&dev));
}

TEST_F(PowerButtonEvdevDelegateTest, IsNotTargetWhenBusTypeIsUsb) {
  test::MockLibevdevWrapper dev;
  ON_CALL(dev, HasEventCode(EV_KEY, KEY_POWER)).WillByDefault(Return(true));
  ON_CALL(dev, GetIdBustype()).WillByDefault(Return(BUS_USB));

  EXPECT_FALSE(delegate_.IsTarget(&dev));
}

TEST_F(PowerButtonEvdevDelegateTest, IsNotTargetWhenNoPowerKey) {
  test::MockLibevdevWrapper dev;
  ON_CALL(dev, HasEventCode(EV_KEY, KEY_POWER)).WillByDefault(Return(false));
  ON_CALL(dev, GetIdBustype()).WillByDefault(Return(BUS_HOST));

  EXPECT_FALSE(delegate_.IsTarget(&dev));
}

TEST_F(PowerButtonEvdevDelegateTest, FireEventButtonUp) {
  test::MockLibevdevWrapper dev;
  input_event ev = {.type = EV_KEY, .code = KEY_POWER, .value = 0};

  mojom::PowerButtonObserver::ButtonState button_state;
  EXPECT_CALL(mock_observer_, OnEvent(_)).WillOnce(SaveArg<0>(&button_state));

  delegate_.FireEvent(ev, &dev);
  receiver_.FlushForTesting();

  EXPECT_EQ(button_state, mojom::PowerButtonObserver::ButtonState::kUp);
}

TEST_F(PowerButtonEvdevDelegateTest, FireEventButtonDown) {
  test::MockLibevdevWrapper dev;
  input_event ev = {.type = EV_KEY, .code = KEY_POWER, .value = 1};

  mojom::PowerButtonObserver::ButtonState button_state;
  EXPECT_CALL(mock_observer_, OnEvent(_)).WillOnce(SaveArg<0>(&button_state));

  delegate_.FireEvent(ev, &dev);
  receiver_.FlushForTesting();

  EXPECT_EQ(button_state, mojom::PowerButtonObserver::ButtonState::kDown);
}

TEST_F(PowerButtonEvdevDelegateTest, FireEventButtonRepeat) {
  test::MockLibevdevWrapper dev;
  input_event ev = {.type = EV_KEY, .code = KEY_POWER, .value = 2};

  mojom::PowerButtonObserver::ButtonState button_state;
  EXPECT_CALL(mock_observer_, OnEvent(_)).WillOnce(SaveArg<0>(&button_state));

  delegate_.FireEvent(ev, &dev);
  receiver_.FlushForTesting();

  EXPECT_EQ(button_state, mojom::PowerButtonObserver::ButtonState::kRepeat);
}

TEST_F(PowerButtonEvdevDelegateTest, FireNoEventWhenInputEventIsInvalid) {
  test::MockLibevdevWrapper dev;
  input_event ev = {.type = EV_KEY, .code = KEY_POWER, .value = 3};

  EXPECT_CALL(mock_observer_, OnEvent(_)).Times(0);

  delegate_.FireEvent(ev, &dev);
  receiver_.FlushForTesting();
}

TEST_F(PowerButtonEvdevDelegateTest, InitializationFail) {
  base::test::TestFuture<uint32_t, const std::string&> future;
  receiver_.set_disconnect_with_reason_handler(future.GetCallback());

  delegate_.InitializationFail(/*custom_reason=*/42,
                               /*description=*/"error description");

  EXPECT_EQ(future.Get<uint32_t>(), 42);
  EXPECT_EQ(future.Get<std::string>(), "error description");
}

TEST_F(PowerButtonEvdevDelegateTest, ReportProperties) {
  bool is_called = false;
  EXPECT_CALL(mock_observer_, OnConnectedToEventNode())
      .WillOnce(Assign(&is_called, true));

  test::MockLibevdevWrapper dev;
  delegate_.ReportProperties(&dev);
  receiver_.FlushForTesting();

  EXPECT_TRUE(is_called);
}

}  // namespace
}  // namespace diagnostics
