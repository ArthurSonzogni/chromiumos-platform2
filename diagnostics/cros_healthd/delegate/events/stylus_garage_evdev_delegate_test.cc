// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/delegate/events/stylus_garage_evdev_delegate.h"

#include <linux/input-event-codes.h>
#include <linux/input.h>

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

using ::testing::Return;

class MockStylusGarageObserver : public mojom::StylusGarageObserver {
 public:
  // ash::cros_healthd::mojom::StylusGarageObserver overrides:
  MOCK_METHOD(void, OnInsert, (), (override));
  MOCK_METHOD(void, OnRemove, (), (override));
};

class StylusGarageEvdevDelegateTest : public ::testing::Test {
 protected:
  base::test::TaskEnvironment task_environment_;
  MockStylusGarageObserver mock_observer_;
  mojo::Receiver<mojom::StylusGarageObserver> receiver_{&mock_observer_};
  StylusGarageEvdevDelegate delegate_{receiver_.BindNewPipeAndPassRemote()};
};

TEST_F(StylusGarageEvdevDelegateTest, IsTarget) {
  test::MockLibevdevWrapper dev;
  ON_CALL(dev, HasEventCode(EV_SW, SW_PEN_INSERTED))
      .WillByDefault(Return(true));

  EXPECT_TRUE(delegate_.IsTarget(&dev));
}

TEST_F(StylusGarageEvdevDelegateTest, IsNotTarget) {
  test::MockLibevdevWrapper dev;
  ON_CALL(dev, HasEventCode(EV_SW, SW_PEN_INSERTED))
      .WillByDefault(Return(false));

  EXPECT_FALSE(delegate_.IsTarget(&dev));
}

TEST_F(StylusGarageEvdevDelegateTest, FireOnInsertEvent) {
  test::MockLibevdevWrapper dev;
  input_event ev = {.type = EV_SW, .code = SW_PEN_INSERTED, .value = 1};

  EXPECT_CALL(mock_observer_, OnInsert()).Times(1);

  delegate_.FireEvent(ev, &dev);
  receiver_.FlushForTesting();
}

TEST_F(StylusGarageEvdevDelegateTest, FireOnRemoveEvent) {
  test::MockLibevdevWrapper dev;
  input_event ev = {.type = EV_SW, .code = SW_PEN_INSERTED, .value = 0};

  EXPECT_CALL(mock_observer_, OnRemove()).Times(1);

  delegate_.FireEvent(ev, &dev);
  receiver_.FlushForTesting();
}

TEST_F(StylusGarageEvdevDelegateTest, FireNoEventForUnexpectedInputEvent) {
  test::MockLibevdevWrapper dev;
  input_event ev = {.type = EV_KEY, .code = KEY_VOLUMEUP, .value = 1};

  EXPECT_CALL(mock_observer_, OnInsert()).Times(0);
  EXPECT_CALL(mock_observer_, OnRemove()).Times(0);

  delegate_.FireEvent(ev, &dev);
  receiver_.FlushForTesting();
}

TEST_F(StylusGarageEvdevDelegateTest, InitializationFail) {
  base::test::TestFuture<uint32_t, const std::string&> future;
  receiver_.set_disconnect_with_reason_handler(future.GetCallback());

  delegate_.InitializationFail(/*custom_reason=*/42,
                               /*description=*/"error description");

  EXPECT_EQ(future.Get<uint32_t>(), 42);
  EXPECT_EQ(future.Get<std::string>(), "error description");
}

TEST_F(StylusGarageEvdevDelegateTest, ReportPropertiesNoCrash) {
  test::MockLibevdevWrapper dev;

  delegate_.ReportProperties(&dev);
  receiver_.FlushForTesting();

  // Stylus garage has no property to report. Check no crash occurred.
  SUCCEED();
}

}  // namespace
}  // namespace diagnostics
