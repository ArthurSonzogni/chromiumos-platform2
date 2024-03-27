// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/delegate/events/volume_button_evdev_delegate.h"

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

using ::testing::Return;

class FakeVolumeButtonObserver : public mojom::VolumeButtonObserver {
 public:
  // ash::cros_healthd::mojom::VolumeButtonObserver overrides:
  void OnEvent(mojom::VolumeButtonObserver::Button button,
               mojom::VolumeButtonObserver::ButtonState button_state) override {
    last_button_ = button;
    last_button_state_ = button_state;
  }

  std::optional<mojom::VolumeButtonObserver::Button> last_button_;
  std::optional<mojom::VolumeButtonObserver::ButtonState> last_button_state_;
};

class VolumeButtonEvdevDelegateTest : public ::testing::Test {
 protected:
  base::test::TaskEnvironment task_environment_;
  FakeVolumeButtonObserver fake_observer_;
  mojo::Receiver<mojom::VolumeButtonObserver> receiver_{&fake_observer_};
  VolumeButtonEvdevDelegate delegate_{receiver_.BindNewPipeAndPassRemote()};
};

TEST_F(VolumeButtonEvdevDelegateTest, IsTarget) {
  test::MockLibevdevWrapper dev;
  ON_CALL(dev, HasEventCode(EV_KEY, KEY_VOLUMEDOWN))
      .WillByDefault(Return(true));
  ON_CALL(dev, HasEventCode(EV_KEY, KEY_VOLUMEUP)).WillByDefault(Return(true));

  EXPECT_TRUE(delegate_.IsTarget(&dev));
}

TEST_F(VolumeButtonEvdevDelegateTest, IsNotTargetWhenHaveNoVolumeKey) {
  test::MockLibevdevWrapper dev;
  ON_CALL(dev, HasEventCode(EV_KEY, KEY_VOLUMEDOWN))
      .WillByDefault(Return(false));
  ON_CALL(dev, HasEventCode(EV_KEY, KEY_VOLUMEUP)).WillByDefault(Return(false));

  EXPECT_FALSE(delegate_.IsTarget(&dev));
}

TEST_F(VolumeButtonEvdevDelegateTest, FireNoEventWhenInputEventIsNotKeyEvent) {
  test::MockLibevdevWrapper dev;
  input_event ev = {.type = EV_SW, .code = SW_HEADPHONE_INSERT, .value = 0};

  delegate_.FireEvent(ev, &dev);
  receiver_.FlushForTesting();

  EXPECT_EQ(fake_observer_.last_button_, std::nullopt);
  EXPECT_EQ(fake_observer_.last_button_state_, std::nullopt);
}

TEST_F(VolumeButtonEvdevDelegateTest, FireNoEventWhenInputEventIsNotVolumeKey) {
  test::MockLibevdevWrapper dev;
  input_event ev = {.type = EV_KEY, .code = KEY_POWER, .value = 0};

  delegate_.FireEvent(ev, &dev);
  receiver_.FlushForTesting();

  EXPECT_EQ(fake_observer_.last_button_, std::nullopt);
  EXPECT_EQ(fake_observer_.last_button_state_, std::nullopt);
}

TEST_F(VolumeButtonEvdevDelegateTest, FireEventVolumeUpButtonUp) {
  test::MockLibevdevWrapper dev;
  input_event ev = {.type = EV_KEY, .code = KEY_VOLUMEUP, .value = 0};

  delegate_.FireEvent(ev, &dev);
  receiver_.FlushForTesting();

  EXPECT_EQ(fake_observer_.last_button_,
            mojom::VolumeButtonObserver::Button::kVolumeUp);
  EXPECT_EQ(fake_observer_.last_button_state_,
            mojom::VolumeButtonObserver::ButtonState::kUp);
}

TEST_F(VolumeButtonEvdevDelegateTest, FireEventVolumeUpButtonDown) {
  test::MockLibevdevWrapper dev;
  input_event ev = {.type = EV_KEY, .code = KEY_VOLUMEUP, .value = 1};

  delegate_.FireEvent(ev, &dev);
  receiver_.FlushForTesting();

  EXPECT_EQ(fake_observer_.last_button_,
            mojom::VolumeButtonObserver::Button::kVolumeUp);
  EXPECT_EQ(fake_observer_.last_button_state_,
            mojom::VolumeButtonObserver::ButtonState::kDown);
}

TEST_F(VolumeButtonEvdevDelegateTest, FireEventVolumeUpButtonRepeat) {
  test::MockLibevdevWrapper dev;
  input_event ev = {.type = EV_KEY, .code = KEY_VOLUMEUP, .value = 2};

  delegate_.FireEvent(ev, &dev);
  receiver_.FlushForTesting();

  EXPECT_EQ(fake_observer_.last_button_,
            mojom::VolumeButtonObserver::Button::kVolumeUp);
  EXPECT_EQ(fake_observer_.last_button_state_,
            mojom::VolumeButtonObserver::ButtonState::kRepeat);
}

TEST_F(VolumeButtonEvdevDelegateTest, FireEventVolumeDownButtonUp) {
  test::MockLibevdevWrapper dev;
  input_event ev = {.type = EV_KEY, .code = KEY_VOLUMEDOWN, .value = 0};

  delegate_.FireEvent(ev, &dev);
  receiver_.FlushForTesting();

  EXPECT_EQ(fake_observer_.last_button_,
            mojom::VolumeButtonObserver::Button::kVolumeDown);
  EXPECT_EQ(fake_observer_.last_button_state_,
            mojom::VolumeButtonObserver::ButtonState::kUp);
}

TEST_F(VolumeButtonEvdevDelegateTest, FireNoEventWhenInputEventIsInvalid) {
  test::MockLibevdevWrapper dev;
  input_event ev = {.type = EV_KEY, .code = KEY_VOLUMEUP, .value = 3};

  delegate_.FireEvent(ev, &dev);
  receiver_.FlushForTesting();

  EXPECT_EQ(fake_observer_.last_button_, std::nullopt);
  EXPECT_EQ(fake_observer_.last_button_state_, std::nullopt);
}

TEST_F(VolumeButtonEvdevDelegateTest, InitializationFail) {
  base::test::TestFuture<uint32_t, const std::string&> future;
  receiver_.set_disconnect_with_reason_handler(future.GetCallback());

  delegate_.InitializationFail(/*custom_reason=*/42,
                               /*description=*/"error description");

  EXPECT_EQ(future.Get<uint32_t>(), 42);
  EXPECT_EQ(future.Get<std::string>(), "error description");
}

}  // namespace
}  // namespace diagnostics
