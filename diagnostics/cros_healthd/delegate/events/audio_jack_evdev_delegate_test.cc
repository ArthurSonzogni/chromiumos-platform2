// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/delegate/events/audio_jack_evdev_delegate.h"

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
using ::testing::Return;
using ::testing::SaveArg;

class MockAudioJackObserver : public mojom::AudioJackObserver {
 public:
  // ash::cros_healthd::mojom::AudioJackObserver overrides:
  MOCK_METHOD(void,
              OnAdd,
              (mojom::AudioJackEventInfo::DeviceType device_type),
              (override));
  MOCK_METHOD(void,
              OnRemove,
              (mojom::AudioJackEventInfo::DeviceType device_type),
              (override));
};

class AudioJackEvdevDelegateTest : public ::testing::Test {
 protected:
  base::test::TaskEnvironment task_environment_;
  MockAudioJackObserver mock_observer_;
  mojo::Receiver<mojom::AudioJackObserver> receiver_{&mock_observer_};
  AudioJackEvdevDelegate delegate_{receiver_.BindNewPipeAndPassRemote()};
};

TEST_F(AudioJackEvdevDelegateTest, IsTargetWithHeadphone) {
  test::MockLibevdevWrapper dev;
  ON_CALL(dev, HasEventCode(EV_SW, SW_HEADPHONE_INSERT))
      .WillByDefault(Return(true));
  ON_CALL(dev, HasEventCode(EV_SW, SW_MICROPHONE_INSERT))
      .WillByDefault(Return(false));

  EXPECT_TRUE(delegate_.IsTarget(&dev));
}

TEST_F(AudioJackEvdevDelegateTest, IsTargetWithMicrophone) {
  test::MockLibevdevWrapper dev;
  ON_CALL(dev, HasEventCode(EV_SW, SW_HEADPHONE_INSERT))
      .WillByDefault(Return(false));
  ON_CALL(dev, HasEventCode(EV_SW, SW_MICROPHONE_INSERT))
      .WillByDefault(Return(true));

  EXPECT_TRUE(delegate_.IsTarget(&dev));
}

TEST_F(AudioJackEvdevDelegateTest, IsNotTarget) {
  test::MockLibevdevWrapper dev;
  ON_CALL(dev, HasEventCode(EV_SW, SW_HEADPHONE_INSERT))
      .WillByDefault(Return(false));
  ON_CALL(dev, HasEventCode(EV_SW, SW_MICROPHONE_INSERT))
      .WillByDefault(Return(false));

  EXPECT_FALSE(delegate_.IsTarget(&dev));
}

TEST_F(AudioJackEvdevDelegateTest, FireEventHeadphoneAdd) {
  test::MockLibevdevWrapper dev;
  input_event ev = {.type = EV_SW, .code = SW_HEADPHONE_INSERT, .value = 1};

  mojom::AudioJackEventInfo::DeviceType device_type;
  EXPECT_CALL(mock_observer_, OnAdd(_)).WillOnce(SaveArg<0>(&device_type));

  delegate_.FireEvent(ev, &dev);
  receiver_.FlushForTesting();

  EXPECT_EQ(device_type, mojom::AudioJackEventInfo::DeviceType::kHeadphone);
}

TEST_F(AudioJackEvdevDelegateTest, FireEventHeadphoneRemove) {
  test::MockLibevdevWrapper dev;
  input_event ev = {.type = EV_SW, .code = SW_HEADPHONE_INSERT, .value = 0};

  mojom::AudioJackEventInfo::DeviceType device_type;
  EXPECT_CALL(mock_observer_, OnRemove(_)).WillOnce(SaveArg<0>(&device_type));

  delegate_.FireEvent(ev, &dev);
  receiver_.FlushForTesting();

  EXPECT_EQ(device_type, mojom::AudioJackEventInfo::DeviceType::kHeadphone);
}

TEST_F(AudioJackEvdevDelegateTest, FireEventMicrophoneAdd) {
  test::MockLibevdevWrapper dev;
  input_event ev = {.type = EV_SW, .code = SW_MICROPHONE_INSERT, .value = 1};

  mojom::AudioJackEventInfo::DeviceType device_type;
  EXPECT_CALL(mock_observer_, OnAdd(_)).WillOnce(SaveArg<0>(&device_type));

  delegate_.FireEvent(ev, &dev);
  receiver_.FlushForTesting();

  EXPECT_EQ(device_type, mojom::AudioJackEventInfo::DeviceType::kMicrophone);
}

TEST_F(AudioJackEvdevDelegateTest, FireEventMicrophoneRemove) {
  test::MockLibevdevWrapper dev;
  input_event ev = {.type = EV_SW, .code = SW_MICROPHONE_INSERT, .value = 0};

  mojom::AudioJackEventInfo::DeviceType device_type;
  EXPECT_CALL(mock_observer_, OnRemove(_)).WillOnce(SaveArg<0>(&device_type));

  delegate_.FireEvent(ev, &dev);
  receiver_.FlushForTesting();

  EXPECT_EQ(device_type, mojom::AudioJackEventInfo::DeviceType::kMicrophone);
}

TEST_F(AudioJackEvdevDelegateTest, FireNoEventForUnexpectedInputEvent) {
  test::MockLibevdevWrapper dev;
  input_event ev = {.type = EV_KEY, .code = KEY_VOLUMEUP, .value = 0};

  EXPECT_CALL(mock_observer_, OnAdd(_)).Times(0);
  EXPECT_CALL(mock_observer_, OnRemove(_)).Times(0);

  delegate_.FireEvent(ev, &dev);
  receiver_.FlushForTesting();
}

TEST_F(AudioJackEvdevDelegateTest, InitializationFail) {
  base::test::TestFuture<uint32_t, const std::string&> future;
  receiver_.set_disconnect_with_reason_handler(future.GetCallback());

  delegate_.InitializationFail(/*custom_reason=*/42,
                               /*description=*/"error description");

  EXPECT_EQ(future.Get<uint32_t>(), 42);
  EXPECT_EQ(future.Get<std::string>(), "error description");
}

TEST_F(AudioJackEvdevDelegateTest, ReportPropertiesNoCrash) {
  test::MockLibevdevWrapper dev;

  delegate_.ReportProperties(&dev);
  receiver_.FlushForTesting();

  // Audio jack has no property to report. Check no crash occurred.
  SUCCEED();
}

}  // namespace
}  // namespace diagnostics
