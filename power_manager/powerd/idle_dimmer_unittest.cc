// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <glib.h>
#include <gtest/gtest.h>

#include "base/command_line.h"
#include "base/logging.h"
#include "power_manager/common/power_constants.h"
#include "power_manager/common/power_prefs.h"
#include "power_manager/powerd/internal_backlight_controller.h"
#include "power_manager/powerd/system/mock_backlight.h"

using ::testing::_;
using ::testing::DoAll;
using ::testing::NotNull;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::SetArgumentPointee;
using ::testing::Test;

namespace power_manager {

static const int64 kIdleBrightness = 1;
static const int64 kDefaultBrightness = 5;
static const int64 kMaxBrightness = 10;
static const int64 kPluggedBrightness = 7;
static const int64 kUnpluggedBrightness = 3;
static const int64 kPluggedBrightnessP = kPluggedBrightness * 100 /
                                                          kMaxBrightness;
static const int64 kUnpluggedBrightnessP = kUnpluggedBrightness * 100 /
                                                          kMaxBrightness;

class IdleDimmerTest : public Test {
 public:
  IdleDimmerTest()
      : prefs_(FilePath("/tmp")),
        backlight_ctl_(&backlight_, &prefs_, NULL),
        current_brightness_(0),
        target_brightness_(0) {
    EXPECT_CALL(backlight_, GetCurrentBrightnessLevel(NotNull()))
        .WillRepeatedly(DoAll(SetArgumentPointee<0>(current_brightness_),
                              Return(true)));
    EXPECT_CALL(backlight_, GetMaxBrightnessLevel(NotNull()))
        .WillRepeatedly(DoAll(SetArgumentPointee<0>(kMaxBrightness),
                              Return(true)));
    EXPECT_CALL(backlight_, SetBrightnessLevel(_, _))
        .WillRepeatedly(DoAll(SaveArg<0>(&current_brightness_),
                              Return(true)));

    prefs_.SetInt64(kPluggedBrightnessOffsetPref, kPluggedBrightnessP);
    prefs_.SetInt64(kUnpluggedBrightnessOffsetPref, kUnpluggedBrightnessP);

    CHECK(backlight_ctl_.Init());
  }

  virtual void SetUp() {
    backlight_ctl_.OnPlugEvent(true);
    backlight_ctl_.SetPowerState(BACKLIGHT_ACTIVE);
  }

 protected:
  system::MockBacklight backlight_;
  PowerPrefs prefs_;
  InternalBacklightController backlight_ctl_;

  int64 current_brightness_;
  int64 target_brightness_;
};

// Test that OnIdleEvent sets the brightness appropriately when
// the user becomes idle.
TEST_F(IdleDimmerTest, TestIdle) {
  EXPECT_CALL(backlight_, GetCurrentBrightnessLevel(NotNull()))
      .WillRepeatedly(DoAll(SetArgumentPointee<0>(kDefaultBrightness),
                            Return(true)));
  EXPECT_CALL(backlight_, GetMaxBrightnessLevel(NotNull()))
      .WillRepeatedly(DoAll(SetArgumentPointee<0>(kMaxBrightness),
                            Return(true)));
  backlight_.ExpectSetBrightnessLevelRepeatedly(kIdleBrightness, true);
  backlight_ctl_.SetPowerState(BACKLIGHT_DIM);
}

// Test that OnIdleEvent does not mess with the user's brightness settings
// when we receive duplicate idle events.
TEST_F(IdleDimmerTest, TestDuplicateIdleEvent) {
  EXPECT_CALL(backlight_, GetCurrentBrightnessLevel(NotNull()))
      .WillRepeatedly(DoAll(SetArgumentPointee<0>(kDefaultBrightness),
                            Return(true)));
  EXPECT_CALL(backlight_, GetMaxBrightnessLevel(NotNull()))
      .WillRepeatedly(DoAll(SetArgumentPointee<0>(kMaxBrightness),
                            Return(true)));
  backlight_.ExpectSetBrightnessLevelRepeatedly(kIdleBrightness, true);
  backlight_ctl_.SetPowerState(BACKLIGHT_DIM);
  backlight_ctl_.SetPowerState(BACKLIGHT_DIM);
}

// Test that OnIdleEvent does not set the brightness when we receive
// an idle event for a non-idle user.
TEST_F(IdleDimmerTest, TestNonIdle) {
  EXPECT_CALL(backlight_, SetBrightnessLevel(kPluggedBrightness, _))
      .Times(0);
  backlight_ctl_.SetPowerState(BACKLIGHT_ACTIVE);
}

// Test that OnIdleEvent sets the brightness appropriately when the
// user transitions to idle and back.
TEST_F(IdleDimmerTest, TestIdleTransition) {
  EXPECT_CALL(backlight_, GetCurrentBrightnessLevel(NotNull()))
      .WillRepeatedly(DoAll(SetArgumentPointee<0>(kDefaultBrightness),
                            Return(true)));
  EXPECT_CALL(backlight_, GetMaxBrightnessLevel(NotNull()))
      .WillRepeatedly(DoAll(SetArgumentPointee<0>(kMaxBrightness),
                            Return(true)));
  backlight_.ExpectSetBrightnessLevelRepeatedly(kIdleBrightness, true);
  backlight_ctl_.SetPowerState(BACKLIGHT_DIM);

  EXPECT_CALL(backlight_, GetCurrentBrightnessLevel(NotNull()))
      .WillRepeatedly(DoAll(SetArgumentPointee<0>(kIdleBrightness + 2),
                            Return(true)));
  EXPECT_CALL(backlight_, GetMaxBrightnessLevel(NotNull()))
      .WillRepeatedly(DoAll(SetArgumentPointee<0>(kMaxBrightness),
                            Return(true)));
  backlight_.ExpectSetBrightnessLevelRepeatedly(kDefaultBrightness + 2, true);
  backlight_ctl_.SetPowerState(BACKLIGHT_ACTIVE);
}

// Test that OnIdleEvent sets the brightness appropriately when the
// user transitions to idle and back, and the max brightness setting
// is reached.
TEST_F(IdleDimmerTest, TestOverflowIdleTransition) {
  EXPECT_CALL(backlight_, GetCurrentBrightnessLevel(NotNull()))
      .WillRepeatedly(DoAll(SetArgumentPointee<0>(kDefaultBrightness),
                            Return(true)));
  EXPECT_CALL(backlight_, GetMaxBrightnessLevel(NotNull()))
      .WillRepeatedly(DoAll(SetArgumentPointee<0>(kMaxBrightness),
                            Return(true)));
  backlight_.ExpectSetBrightnessLevelRepeatedly(kIdleBrightness, true);
  backlight_ctl_.SetPowerState(BACKLIGHT_DIM);

  EXPECT_CALL(backlight_, GetCurrentBrightnessLevel(NotNull()))
      .WillRepeatedly(DoAll(SetArgumentPointee<0>(kMaxBrightness - 1),
                            Return(true)));
  EXPECT_CALL(backlight_, GetMaxBrightnessLevel(NotNull()))
      .WillRepeatedly(DoAll(SetArgumentPointee<0>(kMaxBrightness),
                            Return(true)));
  backlight_.ExpectSetBrightnessLevelRepeatedly(kMaxBrightness, true);
  backlight_ctl_.SetPowerState(BACKLIGHT_ACTIVE);
}

}  // namespace power_manager
