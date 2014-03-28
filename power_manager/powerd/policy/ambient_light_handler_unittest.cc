// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/policy/ambient_light_handler.h"

#include <base/basictypes.h>
#include <base/compiler_specific.h>
#include <gtest/gtest.h>

#include "power_manager/common/fake_prefs.h"
#include "power_manager/common/power_constants.h"
#include "power_manager/powerd/system/ambient_light_sensor_stub.h"

namespace power_manager {
namespace policy {

namespace {

// Pref names.
const char kLimitsPref[] = "limits";
const char kStepsPref[] = "steps";

// AmbientLightHandler::Delegate implementation that records the latest
// brightness percent that was passed to it.
class TestDelegate : public AmbientLightHandler::Delegate {
 public:
  TestDelegate()
      : percent_(-1.0),
        cause_(AmbientLightHandler::CAUSED_BY_AMBIENT_LIGHT) {}
  virtual ~TestDelegate() {}

  double percent() const { return percent_; }
  AmbientLightHandler::BrightnessChangeCause cause() const { return cause_; }

  virtual void SetBrightnessPercentForAmbientLight(
      double brightness_percent,
      AmbientLightHandler::BrightnessChangeCause cause) OVERRIDE {
    percent_ = brightness_percent;
    cause_ = cause;
  }

 private:
  double percent_;
  AmbientLightHandler::BrightnessChangeCause cause_;

  DISALLOW_COPY_AND_ASSIGN(TestDelegate);
};

class AmbientLightHandlerTest : public ::testing::Test {
 public:
  AmbientLightHandlerTest()
      : light_sensor_(0),
        handler_(&light_sensor_, &delegate_),
        initial_brightness_percent_(0.0) {}

  virtual ~AmbientLightHandlerTest() {}

 protected:
  // Initializes |handler_|.
  void Init() {
    prefs_.SetString(kLimitsPref, limits_pref_);
    prefs_.SetString(kStepsPref, steps_pref_);
    light_sensor_.set_lux(initial_lux_);
    handler_.Init(&prefs_, kLimitsPref, kStepsPref,
                  initial_brightness_percent_);
  }

  // Updates the lux level returned by |light_sensor_| and notifies
  // |handler_| about the change.
  void UpdateSensor(int64 lux) {
    light_sensor_.set_lux(lux);
    handler_.OnAmbientLightUpdated(&light_sensor_);
  }

  FakePrefs prefs_;
  system::AmbientLightSensorStub light_sensor_;
  TestDelegate delegate_;
  AmbientLightHandler handler_;

  // Initial values for prefs read by AmbientLightHandler::Init().  Not set
  // in |prefs_| if empty.
  std::string limits_pref_;
  std::string steps_pref_;

  // Initial light level reported by |light_sensor_|.
  int initial_lux_;

  // Initial backlight brightness level passed to AmbientLightHandler::Init().
  double initial_brightness_percent_;

 private:
  DISALLOW_COPY_AND_ASSIGN(AmbientLightHandlerTest);
};

}  // namespace

TEST_F(AmbientLightHandlerTest, UpdatePercent) {
  limits_pref_ = "20.0\n30.0\n100.0";
  steps_pref_ = "20.0 -1 40\n50.0 20 80\n100.0 60 -1";
  initial_lux_ = 50;
  initial_brightness_percent_ = 60.0;
  Init();
  EXPECT_LT(delegate_.percent(), 0.0);

  // The middle step should be used as soon as a light reading is received.
  UpdateSensor(50);
  EXPECT_DOUBLE_EQ(50.0, delegate_.percent());
  EXPECT_EQ(AmbientLightHandler::CAUSED_BY_AMBIENT_LIGHT, delegate_.cause());

  // An initial reading in the lower step should be ignored, but a second
  // reading should overcome hysteresis.
  UpdateSensor(10);
  EXPECT_DOUBLE_EQ(50.0, delegate_.percent());
  UpdateSensor(10);
  EXPECT_DOUBLE_EQ(20.0, delegate_.percent());
  EXPECT_EQ(AmbientLightHandler::CAUSED_BY_AMBIENT_LIGHT, delegate_.cause());

  // Send two high readings and check that the second one causes a jump to
  // the top step.
  UpdateSensor(110);
  EXPECT_DOUBLE_EQ(20.0, delegate_.percent());
  UpdateSensor(90);
  EXPECT_DOUBLE_EQ(100.0, delegate_.percent());
  EXPECT_EQ(AmbientLightHandler::CAUSED_BY_AMBIENT_LIGHT, delegate_.cause());
}

TEST_F(AmbientLightHandlerTest, PowerSources) {
  // Define a single target percent in the bottom step and separate AC and
  // battery targets for the middle and top steps.
  limits_pref_ = "20.0\n30.0\n100.0";
  steps_pref_ = "20.0 -1 40\n50.0 40.0 20 80\n100.0 90.0 60 -1";
  initial_lux_ = 0;
  initial_brightness_percent_ = 10.0;
  Init();
  EXPECT_LT(delegate_.percent(), 0.0);

  // No changes should be made when switching to battery power at the
  // bottom step.
  UpdateSensor(0);
  EXPECT_DOUBLE_EQ(20.0, delegate_.percent());
  EXPECT_EQ(AmbientLightHandler::CAUSED_BY_AMBIENT_LIGHT, delegate_.cause());
  handler_.HandlePowerSourceChange(POWER_BATTERY);
  EXPECT_DOUBLE_EQ(20.0, delegate_.percent());

  // Check that the brightness is updated in response to power source
  // changes while at the middle and top steps.
  UpdateSensor(50);
  UpdateSensor(50);
  EXPECT_DOUBLE_EQ(40.0, delegate_.percent());
  EXPECT_EQ(AmbientLightHandler::CAUSED_BY_AMBIENT_LIGHT, delegate_.cause());
  handler_.HandlePowerSourceChange(POWER_AC);
  EXPECT_DOUBLE_EQ(50.0, delegate_.percent());
  EXPECT_EQ(AmbientLightHandler::CAUSED_BY_POWER_SOURCE, delegate_.cause());

  UpdateSensor(100);
  UpdateSensor(100);
  EXPECT_DOUBLE_EQ(100.0, delegate_.percent());
  EXPECT_EQ(AmbientLightHandler::CAUSED_BY_AMBIENT_LIGHT, delegate_.cause());
  handler_.HandlePowerSourceChange(POWER_BATTERY);
  EXPECT_DOUBLE_EQ(90.0, delegate_.percent());
  EXPECT_EQ(AmbientLightHandler::CAUSED_BY_POWER_SOURCE, delegate_.cause());
}

TEST_F(AmbientLightHandlerTest, NoSteps) {
  // If no steps are defined, the max target percent should be used.
  limits_pref_ = "10.0\n30.0\n80.0";
  initial_lux_ = 0;
  initial_brightness_percent_ = 50.0;
  Init();
  EXPECT_LT(delegate_.percent(), 0.0);

  UpdateSensor(0);
  EXPECT_DOUBLE_EQ(80.0, delegate_.percent());
  UpdateSensor(100);
  UpdateSensor(100);
  EXPECT_DOUBLE_EQ(80.0, delegate_.percent());
}

TEST_F(AmbientLightHandlerTest, DeferInitialChange) {
  limits_pref_ = "20.0\n30.0\n100.0";
  steps_pref_ = "80.0 30.0 -1 400\n100.0 100 -1";
  initial_lux_ = 0;
  initial_brightness_percent_ = 60.0;

  // Power source changes before the ambient light has been measured
  // shouldn't trigger changes.
  Init();
  EXPECT_LT(delegate_.percent(), 0.0);
  handler_.HandlePowerSourceChange(POWER_BATTERY);
  EXPECT_LT(delegate_.percent(), 0.0);

  // After the first ambient light reading, the battery percent from the
  // bottom step should be used.
  UpdateSensor(0);
  EXPECT_DOUBLE_EQ(30.0, delegate_.percent());
  EXPECT_EQ(AmbientLightHandler::CAUSED_BY_AMBIENT_LIGHT, delegate_.cause());
}

}  // namespace policy
}  // namespace power_manager
