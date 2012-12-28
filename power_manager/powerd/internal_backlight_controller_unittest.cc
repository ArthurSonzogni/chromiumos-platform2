// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <utility>
#include <vector>

#include "base/logging.h"
#include "power_manager/common/power_constants.h"
#include "power_manager/common/power_prefs.h"
#include "power_manager/powerd/internal_backlight_controller.h"
#include "power_manager/powerd/mock_ambient_light_sensor.h"
#include "power_manager/powerd/mock_backlight_controller_observer.h"
#include "power_manager/powerd/mock_monitor_reconfigure.h"
#include "power_manager/powerd/system/mock_backlight.h"

using ::testing::_;
using ::testing::DoAll;
using ::testing::Mock;
using ::testing::NotNull;
using ::testing::Return;
using ::testing::SetArgumentPointee;

namespace power_manager {

namespace {

const int64 kDefaultBrightnessLevel = 512;
const int64 kMaxBrightnessLevel = 1024;
const double kPluggedBrightnessPercent = 70.0;
const double kUnpluggedBrightnessPercent = 30.0;

// Repeating either increase or decrease brightness this many times should
// always leave the brightness at a limit.
const int kStepsToHitLimit = 20;

// Number of ambient light sensor samples that should be supplied in order to
// trigger an update to InternalBacklightController's ALS offset.
const int kAlsSamplesToTriggerAdjustment = 5;

}  // namespace

class InternalBacklightControllerTest : public ::testing::Test {
 public:
  InternalBacklightControllerTest()
      : prefs_(FilePath(".")),
        controller_(&backlight_, &prefs_, &light_sensor_) {
  }

  virtual void SetUp() {
    EXPECT_CALL(backlight_, GetCurrentBrightnessLevel(NotNull()))
        .WillRepeatedly(DoAll(SetArgumentPointee<0>(kDefaultBrightnessLevel),
                              Return(true)));
    EXPECT_CALL(backlight_, GetMaxBrightnessLevel(NotNull()))
        .WillRepeatedly(DoAll(SetArgumentPointee<0>(kMaxBrightnessLevel),
                              Return(true)));
    EXPECT_CALL(backlight_, SetBrightnessLevel(_, _))
        .WillRepeatedly(Return(false));
    prefs_.SetDouble(kPluggedBrightnessOffsetPref, kPluggedBrightnessPercent);
    prefs_.SetDouble(kUnpluggedBrightnessOffsetPref,
                     kUnpluggedBrightnessPercent);
    prefs_.SetInt64(kMinVisibleBacklightLevelPref, 1);
    light_sensor_.ExpectAddObserver(&controller_);
    CHECK(controller_.Init());
  }

  virtual void TearDown() {
    if (controller_.light_sensor_ != NULL)
      light_sensor_.ExpectRemoveObserver(&controller_);
  }

 protected:
  ::testing::StrictMock<system::MockBacklight> backlight_;
  ::testing::StrictMock<MockAmbientLightSensor> light_sensor_;
  PowerPrefs prefs_;
  InternalBacklightController controller_;
};

TEST_F(InternalBacklightControllerTest, IncreaseBrightness) {
  ASSERT_TRUE(controller_.SetPowerState(BACKLIGHT_ACTIVE));
  ASSERT_TRUE(controller_.OnPlugEvent(false));
#ifdef HAS_ALS
  EXPECT_DOUBLE_EQ(controller_.LevelToPercent(kDefaultBrightnessLevel),
                   controller_.GetTargetBrightnessPercent());
#else
  EXPECT_DOUBLE_EQ(kUnpluggedBrightnessPercent,
                   controller_.GetTargetBrightnessPercent());
#endif  // defined(HAS_ALS)

  double old_percent = controller_.GetTargetBrightnessPercent();
  controller_.IncreaseBrightness(BRIGHTNESS_CHANGE_AUTOMATED);
  // Check that the first step increases the brightness; within the loop
  // will just ensure that the brightness never decreases.
  EXPECT_GT(controller_.GetTargetBrightnessPercent(), old_percent);

  for (int i = 0; i < kStepsToHitLimit; ++i) {
    old_percent = controller_.GetTargetBrightnessPercent();
    controller_.IncreaseBrightness(BRIGHTNESS_CHANGE_USER_INITIATED);
    EXPECT_GE(controller_.GetTargetBrightnessPercent(), old_percent);
  }

  EXPECT_DOUBLE_EQ(100.0, controller_.GetTargetBrightnessPercent());
}

TEST_F(InternalBacklightControllerTest, DecreaseBrightness) {
  ASSERT_TRUE(controller_.SetPowerState(BACKLIGHT_ACTIVE));
  ASSERT_TRUE(controller_.OnPlugEvent(true));
#ifdef HAS_ALS
  EXPECT_DOUBLE_EQ(controller_.LevelToPercent(kDefaultBrightnessLevel),
                   controller_.GetTargetBrightnessPercent());
#else
  EXPECT_DOUBLE_EQ(kPluggedBrightnessPercent,
                   controller_.GetTargetBrightnessPercent());
#endif  // defined(HAS_ALS)

  double old_percent = controller_.GetTargetBrightnessPercent();
  controller_.DecreaseBrightness(true, BRIGHTNESS_CHANGE_AUTOMATED);
  // Check that the first step decreases the brightness; within the loop
  // will just ensure that the brightness never increases.
  EXPECT_LT(controller_.GetTargetBrightnessPercent(), old_percent);

  for (int i = 0; i < kStepsToHitLimit; ++i) {
    old_percent = controller_.GetTargetBrightnessPercent();
    controller_.DecreaseBrightness(true, BRIGHTNESS_CHANGE_USER_INITIATED);
    EXPECT_LE(controller_.GetTargetBrightnessPercent(), old_percent);
  }

  // Backlight should now be off.
  EXPECT_EQ(0, controller_.GetTargetBrightnessPercent());
}

TEST_F(InternalBacklightControllerTest, DecreaseBrightnessDisallowOff) {
  ASSERT_TRUE(controller_.SetPowerState(BACKLIGHT_ACTIVE));
  ASSERT_TRUE(controller_.OnPlugEvent(true));
#ifdef HAS_ALS
  EXPECT_DOUBLE_EQ(controller_.LevelToPercent(kDefaultBrightnessLevel),
                   controller_.GetTargetBrightnessPercent());
#else
  EXPECT_DOUBLE_EQ(kPluggedBrightnessPercent,
                   controller_.GetTargetBrightnessPercent());
#endif  // defined(HAS_ALS)

  for (int i = 0; i < kStepsToHitLimit; ++i)
    controller_.DecreaseBrightness(false, BRIGHTNESS_CHANGE_USER_INITIATED);

  // Backlight must still be on.
  EXPECT_GT(controller_.GetTargetBrightnessPercent(), 0.0);
}

TEST_F(InternalBacklightControllerTest, DecreaseBrightnessDisallowOffAuto) {
  ASSERT_TRUE(controller_.SetPowerState(BACKLIGHT_ACTIVE));
  ASSERT_TRUE(controller_.OnPlugEvent(true));

  for (int i = 0; i < kStepsToHitLimit; ++i)
    controller_.DecreaseBrightness(false, BRIGHTNESS_CHANGE_AUTOMATED);

  // Backlight must still be on, even after a few state transitions.
  EXPECT_GT(controller_.GetTargetBrightnessPercent(), 0.0);
  ASSERT_TRUE(controller_.SetPowerState(BACKLIGHT_DIM));
  EXPECT_GT(controller_.GetTargetBrightnessPercent(), 0.0);
  ASSERT_TRUE(controller_.SetPowerState(BACKLIGHT_ACTIVE));
  EXPECT_GT(controller_.GetTargetBrightnessPercent(), 0.0);
}

// Test that InternalBacklightController notifies its observer in response to
// brightness changes.
TEST_F(InternalBacklightControllerTest, NotifyObserver) {
  // Set an initial state.
  ASSERT_TRUE(controller_.SetPowerState(BACKLIGHT_ACTIVE));
  ASSERT_TRUE(controller_.OnPlugEvent(false));

  light_sensor_.ExpectGetAmbientLightPercent(16);
  controller_.OnAmbientLightChanged(&light_sensor_);
  Mock::VerifyAndClearExpectations(&light_sensor_);

  MockBacklightControllerObserver observer;
  controller_.SetObserver(&observer);

  // Increase the brightness and check that the observer is notified.
  observer.Clear();
  controller_.IncreaseBrightness(BRIGHTNESS_CHANGE_AUTOMATED);
  ASSERT_EQ(1, static_cast<int>(observer.changes().size()));
  EXPECT_DOUBLE_EQ(controller_.GetTargetBrightnessPercent(),
                   observer.changes()[0].percent);
  EXPECT_EQ(BRIGHTNESS_CHANGE_AUTOMATED, observer.changes()[0].cause);

  // Decrease the brightness.
  observer.Clear();
  controller_.DecreaseBrightness(true, BRIGHTNESS_CHANGE_USER_INITIATED);
  ASSERT_EQ(1, static_cast<int>(observer.changes().size()));
  EXPECT_DOUBLE_EQ(controller_.GetTargetBrightnessPercent(),
                   observer.changes()[0].percent);
  EXPECT_EQ(BRIGHTNESS_CHANGE_USER_INITIATED, observer.changes()[0].cause);

  // Send enough ambient light sensor samples to trigger a brightness change.
  observer.Clear();
  double old_percent = controller_.GetTargetBrightnessPercent();
  for (int i = 0; i < kAlsSamplesToTriggerAdjustment; ++i) {
    light_sensor_.ExpectGetAmbientLightPercent(32);
    controller_.OnAmbientLightChanged(&light_sensor_);
    Mock::VerifyAndClearExpectations(&light_sensor_);
  }
  ASSERT_NE(old_percent, controller_.GetTargetBrightnessPercent());
  ASSERT_EQ(1, static_cast<int>(observer.changes().size()));
  EXPECT_DOUBLE_EQ(controller_.GetTargetBrightnessPercent(),
                   observer.changes()[0].percent);
  EXPECT_EQ(BRIGHTNESS_CHANGE_AUTOMATED, observer.changes()[0].cause);

  // Plug the device in.
  observer.Clear();
  ASSERT_TRUE(controller_.OnPlugEvent(true));
  ASSERT_EQ(1, static_cast<int>(observer.changes().size()));
  EXPECT_DOUBLE_EQ(controller_.GetTargetBrightnessPercent(),
                   observer.changes()[0].percent);
  EXPECT_EQ(BRIGHTNESS_CHANGE_AUTOMATED, observer.changes()[0].cause);

  // Dim the backlight.
  observer.Clear();
  ASSERT_TRUE(controller_.SetPowerState(BACKLIGHT_DIM));
  ASSERT_EQ(1, static_cast<int>(observer.changes().size()));
  EXPECT_DOUBLE_EQ(controller_.GetTargetBrightnessPercent(),
                   observer.changes()[0].percent);
  EXPECT_EQ(BRIGHTNESS_CHANGE_AUTOMATED, observer.changes()[0].cause);
}

// Test that we don't drop the backlight level to 0 in response to automated
// changes: http://crosbug.com/25995
TEST_F(InternalBacklightControllerTest, KeepBacklightOnAfterAutomatedChange) {
  // Set the ALS offset to 100% and then manually lower the brightness as far as
  // we can.
  ASSERT_TRUE(controller_.SetPowerState(BACKLIGHT_ACTIVE));
  ASSERT_TRUE(controller_.OnPlugEvent(true));
  for (int i = 0; i < kAlsSamplesToTriggerAdjustment; ++i) {
    light_sensor_.ExpectGetAmbientLightPercent(100.0);
    controller_.OnAmbientLightChanged(&light_sensor_);
    Mock::VerifyAndClearExpectations(&light_sensor_);
  }
  for (int i = 0; i < kStepsToHitLimit; ++i)
    controller_.DecreaseBrightness(false, BRIGHTNESS_CHANGE_USER_INITIATED);

  // After we set the ALS offset to 0%, the backlight should still be on.
  for (int i = 0; i < kAlsSamplesToTriggerAdjustment; ++i) {
    light_sensor_.ExpectGetAmbientLightPercent(0.0);
    controller_.OnAmbientLightChanged(&light_sensor_);
    Mock::VerifyAndClearExpectations(&light_sensor_);
  }
  EXPECT_GT(controller_.GetTargetBrightnessPercent(), 0.0);
}

TEST_F(InternalBacklightControllerTest, MinBrightnessLevel) {
  // Set a minimum visible backlight level and reinitialize to load it.
  const int kMinLevel = 100;
  prefs_.SetInt64(kMinVisibleBacklightLevelPref, kMinLevel);
  light_sensor_.ExpectAddObserver(&controller_);
  ASSERT_TRUE(controller_.Init());
  ASSERT_TRUE(controller_.SetPowerState(BACKLIGHT_ACTIVE));
  ASSERT_TRUE(controller_.OnPlugEvent(true));

  const double kMinPercent = InternalBacklightController::kMinVisiblePercent;
  EXPECT_DOUBLE_EQ(kMinPercent, controller_.LevelToPercent(kMinLevel));

  // Increase the brightness and check that we hit the max.
  for (int i = 0; i < kStepsToHitLimit; ++i)
    controller_.IncreaseBrightness(BRIGHTNESS_CHANGE_USER_INITIATED);
  EXPECT_DOUBLE_EQ(100.0, controller_.GetTargetBrightnessPercent());

  // Decrease the brightness with allow_off=false and check that we stop when we
  // get to the minimum level that we set in the pref.
  for (int i = 0; i < kStepsToHitLimit; ++i)
    controller_.DecreaseBrightness(false, BRIGHTNESS_CHANGE_USER_INITIATED);
  EXPECT_DOUBLE_EQ(kMinPercent, controller_.GetTargetBrightnessPercent());
  EXPECT_EQ(kMinLevel, controller_.target_level_for_testing());

  // Decrease again with allow_off=true and check that we turn the backlight
  // off.
  for (int i = 0; i < kStepsToHitLimit; ++i)
    controller_.DecreaseBrightness(true, BRIGHTNESS_CHANGE_USER_INITIATED);
  EXPECT_DOUBLE_EQ(0.0, controller_.GetTargetBrightnessPercent());
  EXPECT_EQ(0, controller_.target_level_for_testing());

  // Increase again and check that we go to the minimum level.
  controller_.IncreaseBrightness(BRIGHTNESS_CHANGE_USER_INITIATED);
  EXPECT_DOUBLE_EQ(kMinPercent, controller_.GetTargetBrightnessPercent());
  EXPECT_EQ(kMinLevel, controller_.target_level_for_testing());

  // Now set a lower minimum visible level and check that we don't overshoot it
  // when increasing from the backlight-off state.
  const int kNewMinLevel = 10;
  prefs_.SetInt64(kMinVisibleBacklightLevelPref, kNewMinLevel);
  light_sensor_.ExpectAddObserver(&controller_);
  ASSERT_TRUE(controller_.Init());

  // The minimum level should be mapped to the same percentage as before.
  EXPECT_DOUBLE_EQ(kMinPercent, controller_.LevelToPercent(kNewMinLevel));
  for (int i = 0; i < kStepsToHitLimit; ++i)
    controller_.DecreaseBrightness(true, BRIGHTNESS_CHANGE_USER_INITIATED);
  EXPECT_DOUBLE_EQ(0.0, controller_.GetTargetBrightnessPercent());
  EXPECT_EQ(0, controller_.target_level_for_testing());

  controller_.IncreaseBrightness(BRIGHTNESS_CHANGE_USER_INITIATED);
  EXPECT_DOUBLE_EQ(kMinPercent, controller_.GetTargetBrightnessPercent());
  EXPECT_EQ(kNewMinLevel, controller_.target_level_for_testing());

  // Sending another increase request should raise the brightness above the
  // minimum visible level.
  controller_.IncreaseBrightness(BRIGHTNESS_CHANGE_USER_INITIATED);
  EXPECT_GT(controller_.GetTargetBrightnessPercent(), kMinPercent);
  EXPECT_GT(controller_.target_level_for_testing(), kNewMinLevel);
}

// Test the case where the minimum visible backlight level matches the maximum
// level exposed by hardware.
TEST_F(InternalBacklightControllerTest, MinBrightnessLevelMatchesMax) {
  prefs_.SetInt64(kMinVisibleBacklightLevelPref, kMaxBrightnessLevel);
  light_sensor_.ExpectAddObserver(&controller_);
  ASSERT_TRUE(controller_.Init());
#ifdef HAS_ALS
  // The controller avoids adjusting the brightness until it gets its first
  // reading from the ambient light sensor.
  light_sensor_.ExpectGetAmbientLightPercent(0.0);
  controller_.OnAmbientLightChanged(&light_sensor_);
  Mock::VerifyAndClearExpectations(&light_sensor_);
#endif
  ASSERT_TRUE(controller_.SetPowerState(BACKLIGHT_ACTIVE));
  ASSERT_TRUE(controller_.OnPlugEvent(true));
  EXPECT_DOUBLE_EQ(100.0, controller_.GetTargetBrightnessPercent());

  // Decrease the brightness with allow_off=false.
  controller_.DecreaseBrightness(false, BRIGHTNESS_CHANGE_USER_INITIATED);
  EXPECT_DOUBLE_EQ(100.0, controller_.GetTargetBrightnessPercent());

  // Decrease again with allow_off=true.
  controller_.DecreaseBrightness(true, BRIGHTNESS_CHANGE_USER_INITIATED);
  EXPECT_DOUBLE_EQ(0.0, controller_.GetTargetBrightnessPercent());
}

// Test the saved brightness level before and after suspend.
TEST_F(InternalBacklightControllerTest, SuspendBrightnessLevel) {
#ifdef HAS_ALS
  // The controller avoids adjusting the brightness until it gets its first
  // reading from the ambient light sensor.
  light_sensor_.ExpectGetAmbientLightPercent(0.0);
  controller_.OnAmbientLightChanged(&light_sensor_);
  Mock::VerifyAndClearExpectations(&light_sensor_);
#endif

  ASSERT_TRUE(controller_.SetPowerState(BACKLIGHT_ACTIVE));
  ASSERT_TRUE(controller_.OnPlugEvent(true));
  EXPECT_DOUBLE_EQ(kPluggedBrightnessPercent,
                   controller_.GetTargetBrightnessPercent());

  ::testing::StrictMock<MockMonitorReconfigure> monitor;
  controller_.SetMonitorReconfigure(&monitor);

  // Test suspend and resume.
  ASSERT_TRUE(controller_.SetPowerState(BACKLIGHT_SUSPENDED));
  EXPECT_DOUBLE_EQ(kPluggedBrightnessPercent,
                   controller_.GetTargetBrightnessPercent());
  Mock::VerifyAndClearExpectations(&monitor);

  ASSERT_TRUE(controller_.SetPowerState(BACKLIGHT_ACTIVE));
  EXPECT_DOUBLE_EQ(kPluggedBrightnessPercent,
                   controller_.GetTargetBrightnessPercent());
  Mock::VerifyAndClearExpectations(&monitor);

  // Test idling into suspend state.  The backlight should be at 0% after the
  // display is turned off, but it should be set back to the active level (with
  // the screen still off) before suspending, so that the kernel driver can
  // restore that level after resuming.
  ASSERT_TRUE(controller_.SetPowerState(BACKLIGHT_DIM));
  Mock::VerifyAndClearExpectations(&monitor);

  // We can't check that |monitor| is told to turn off all displays here, since
  // we schedule an animated transition to 0 and don't turn the displays off
  // until it's done. :-(
  ASSERT_TRUE(controller_.SetPowerState(BACKLIGHT_IDLE_OFF));
  EXPECT_DOUBLE_EQ(0.0, controller_.GetTargetBrightnessPercent());

  ASSERT_TRUE(controller_.SetPowerState(BACKLIGHT_SUSPENDED));
  EXPECT_DOUBLE_EQ(kPluggedBrightnessPercent,
                   controller_.GetTargetBrightnessPercent());
  Mock::VerifyAndClearExpectations(&monitor);

  // Test resume.
  monitor.ExpectRequest(OUTPUT_SELECTION_ALL_DISPLAYS, POWER_STATE_ON);
  ASSERT_TRUE(controller_.SetPowerState(BACKLIGHT_ACTIVE));
  EXPECT_DOUBLE_EQ(kPluggedBrightnessPercent,
                   controller_.GetTargetBrightnessPercent());
  Mock::VerifyAndClearExpectations(&monitor);

  controller_.SetMonitorReconfigure(NULL);
}

// Check that InternalBacklightController reinitializes itself correctly when
// the backlight device changes (i.e. a new monitor is connected).
TEST_F(InternalBacklightControllerTest, ChangeBacklightDevice) {
  ASSERT_TRUE(controller_.SetPowerState(BACKLIGHT_ACTIVE));
  ASSERT_TRUE(controller_.OnPlugEvent(false));
  for (int i = 0; i < kStepsToHitLimit; ++i)
    controller_.IncreaseBrightness(BRIGHTNESS_CHANGE_USER_INITIATED);
  EXPECT_DOUBLE_EQ(100.0, controller_.GetTargetBrightnessPercent());

  // Update the backlight to expose a [0, 1] range.
  const int64 kNewMaxBrightnessLevel = 1;
  EXPECT_CALL(backlight_, GetMaxBrightnessLevel(NotNull()))
      .WillRepeatedly(DoAll(SetArgumentPointee<0>(kNewMaxBrightnessLevel),
                            Return(true)));
  EXPECT_CALL(backlight_, GetCurrentBrightnessLevel(NotNull()))
      .WillRepeatedly(DoAll(SetArgumentPointee<0>(kNewMaxBrightnessLevel),
                            Return(true)));

  // Check that there's a single step between 100% and 0%.
  light_sensor_.ExpectAddObserver(&controller_);
  controller_.OnBacklightDeviceChanged();
  EXPECT_DOUBLE_EQ(100.0, controller_.GetTargetBrightnessPercent());
  controller_.DecreaseBrightness(false, BRIGHTNESS_CHANGE_USER_INITIATED);
  EXPECT_DOUBLE_EQ(100.0, controller_.GetTargetBrightnessPercent());
  controller_.DecreaseBrightness(true, BRIGHTNESS_CHANGE_USER_INITIATED);
  EXPECT_DOUBLE_EQ(0.0, controller_.GetTargetBrightnessPercent());
  controller_.IncreaseBrightness(BRIGHTNESS_CHANGE_USER_INITIATED);
  EXPECT_DOUBLE_EQ(100.0, controller_.GetTargetBrightnessPercent());

  // Make the backlight expose the original range again.
  EXPECT_CALL(backlight_, GetMaxBrightnessLevel(NotNull()))
      .WillRepeatedly(DoAll(SetArgumentPointee<0>(kMaxBrightnessLevel),
                            Return(true)));
  EXPECT_CALL(backlight_, GetCurrentBrightnessLevel(NotNull()))
      .WillRepeatedly(DoAll(SetArgumentPointee<0>(kMaxBrightnessLevel),
                            Return(true)));

  // We should permit more steps now.
  light_sensor_.ExpectAddObserver(&controller_);
  controller_.OnBacklightDeviceChanged();
  EXPECT_DOUBLE_EQ(100.0, controller_.GetTargetBrightnessPercent());
  controller_.DecreaseBrightness(false, BRIGHTNESS_CHANGE_USER_INITIATED);
  EXPECT_LT(controller_.GetTargetBrightnessPercent(), 100.0);
  EXPECT_GT(controller_.GetTargetBrightnessPercent(), 0.0);
}

// Test that we use a linear mapping between brightness levels and percentages
// when a small range of levels is exposed by the hardware and that we use a
// non-linear mapping when a large range is exposed.
TEST_F(InternalBacklightControllerTest, NonLinearMapping) {
  ASSERT_TRUE(controller_.SetPowerState(BACKLIGHT_ACTIVE));
  ASSERT_TRUE(controller_.OnPlugEvent(false));

  // Update the backlight to expose a tiny range of levels.
  const int64 kSmallMaxBrightnessLevel = 10;
  EXPECT_CALL(backlight_, GetMaxBrightnessLevel(NotNull()))
      .WillRepeatedly(DoAll(SetArgumentPointee<0>(kSmallMaxBrightnessLevel),
                            Return(true)));
  EXPECT_CALL(backlight_, GetCurrentBrightnessLevel(NotNull()))
      .WillRepeatedly(DoAll(SetArgumentPointee<0>(kSmallMaxBrightnessLevel),
                            Return(true)));

  light_sensor_.ExpectAddObserver(&controller_);
  controller_.OnBacklightDeviceChanged();

  EXPECT_DOUBLE_EQ(0, controller_.LevelToPercent(0));
  EXPECT_EQ(static_cast<int64>(0), controller_.PercentToLevel(0.0));

  // The minimum visible level should use the bottom brightness step's
  // percentage, and above it, there should be a linear mapping between levels
  // and percentages.
  const double kMinVisiblePercent =
      InternalBacklightController::kMinVisiblePercent;
  for (int i = 1; i <= kSmallMaxBrightnessLevel; ++i) {
    double percent = kMinVisiblePercent +
        (100.0 - kMinVisiblePercent) * (i - 1) / (kSmallMaxBrightnessLevel - 1);
    EXPECT_DOUBLE_EQ(percent, controller_.LevelToPercent(i));
    EXPECT_EQ(static_cast<int64>(i), controller_.PercentToLevel(percent));
  }

  // With a large range, we should provide more granularity at the bottom end.
  const int64 kLargeMaxBrightnessLevel = 1000;
  EXPECT_CALL(backlight_, GetMaxBrightnessLevel(NotNull()))
      .WillRepeatedly(DoAll(SetArgumentPointee<0>(kLargeMaxBrightnessLevel),
                            Return(true)));
  EXPECT_CALL(backlight_, GetCurrentBrightnessLevel(NotNull()))
      .WillRepeatedly(DoAll(SetArgumentPointee<0>(kLargeMaxBrightnessLevel),
                            Return(true)));
  light_sensor_.ExpectAddObserver(&controller_);
  controller_.OnBacklightDeviceChanged();

  EXPECT_DOUBLE_EQ(0.0, controller_.LevelToPercent(0));
  EXPECT_GT(controller_.LevelToPercent(kLargeMaxBrightnessLevel / 2), 50.0);
  EXPECT_DOUBLE_EQ(100.0, controller_.LevelToPercent(kLargeMaxBrightnessLevel));

  EXPECT_EQ(0, controller_.PercentToLevel(0.0));
  EXPECT_LT(controller_.PercentToLevel(50.0), kLargeMaxBrightnessLevel / 2);
  EXPECT_EQ(kLargeMaxBrightnessLevel, controller_.PercentToLevel(100.0));
}

#ifdef HAS_ALS
TEST_F(InternalBacklightControllerTest, AmbientLightTransitions) {
  ASSERT_TRUE(controller_.SetPowerState(BACKLIGHT_ACTIVE));
  ASSERT_TRUE(controller_.OnPlugEvent(true));

  // The controller should leave the initial brightness unchanged before it's
  // received a reading from the ambient light sensor.
  int64 initial_target_level = controller_.target_level_for_testing();
  EXPECT_EQ(kDefaultBrightnessLevel, initial_target_level);

  // After getting the first reading from the sensor, we should do a slow
  // transition to a lower level.
  light_sensor_.ExpectGetAmbientLightPercent(0.0);
  controller_.OnAmbientLightChanged(&light_sensor_);
  Mock::VerifyAndClearExpectations(&light_sensor_);
  int64 updated_target_level = controller_.target_level_for_testing();
  EXPECT_LT(updated_target_level, initial_target_level);
  EXPECT_EQ(TRANSITION_SLOW, controller_.last_transition_style_for_testing());

  // Pass a bunch of 100% readings and check that we slowly increase the
  // brightness.
  for (int i = 0; i < kAlsSamplesToTriggerAdjustment; ++i) {
    light_sensor_.ExpectGetAmbientLightPercent(100.0);
    controller_.OnAmbientLightChanged(&light_sensor_);
    Mock::VerifyAndClearExpectations(&light_sensor_);
  }
  EXPECT_GT(controller_.target_level_for_testing(), updated_target_level);
  EXPECT_EQ(TRANSITION_SLOW, controller_.last_transition_style_for_testing());
}
#endif

}  // namespace power_manager
