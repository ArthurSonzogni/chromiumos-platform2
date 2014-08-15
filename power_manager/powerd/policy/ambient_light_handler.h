// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_POLICY_AMBIENT_LIGHT_HANDLER_H_
#define POWER_MANAGER_POWERD_POLICY_AMBIENT_LIGHT_HANDLER_H_

#include <string>
#include <vector>

#include <base/basictypes.h>
#include <base/compiler_specific.h>

#include "power_manager/common/power_constants.h"
#include "power_manager/powerd/system/ambient_light_observer.h"

namespace power_manager {

namespace system {
class AmbientLightSensorInterface;
}  // namespace system

namespace policy {

// Observes changes to ambient light reported by system::AmbientLightSensor
// and makes decisions about when backlight brightness should be adjusted.
class AmbientLightHandler : public system::AmbientLightObserver {
 public:
  enum BrightnessChangeCause {
    CAUSED_BY_AMBIENT_LIGHT = 0,
    CAUSED_BY_POWER_SOURCE,
  };

  // Interface for classes that perform actions on behalf of
  // AmbientLightHandler.
  class Delegate {
   public:
    Delegate() {}
    virtual ~Delegate() {}

    // Invoked when the backlight brightness should be adjusted in response
    // to a change in ambient light.
    virtual void SetBrightnessPercentForAmbientLight(
        double brightness_percent,
        BrightnessChangeCause cause) = 0;
  };

  AmbientLightHandler(system::AmbientLightSensorInterface* sensor,
                      Delegate* delegate);
  virtual ~AmbientLightHandler();

  void set_name(const std::string& name) { name_ = name; }

  // Initializes the object based on the data in |steps_pref_value|.
  // |lux_level_| is initialized to a synthetic value based on
  // |initial_brightness_percent|, the backlight brightness at the time of
  // initialization.
  //
  // |steps_pref_value| should contain one or more newline-separated brightness
  // steps, each containing three or four space-separated values:
  //
  //   <ac-backlight-percentage>
  //     <battery-backlight-percentage> (optional)
  //     <decrease-lux-threshold>
  //     <increase-lux-threshold>
  //
  // These values' meanings are described in more detail in BrightnessStep.
  //
  // Steps should be listed in ascending order when sorted by their thresholds,
  // and thresholds should overlap. For example, consider the following steps:
  //
  //    50.0   -1  100
  //    75.0   80  220
  //   100.0  200   -1
  //
  // A brightness level of 50% (corresponding to the bottom step) will be used
  // in conjunction with a starting ALS level of 25. After the ALS increases
  // above 100 (the bottom step's increase threshold), the brightness will
  // increase to 75% (the middle step), and after it increases above 220 (the
  // middle step's increase threshold), 100% (the top step) will be used. If the
  // ALS later falls below 200 (the top step's decrease threshold), 75% will be
  // used, and if it then falls below 80 (the middle step's decrease threshold),
  // 50% will be used.
  void Init(const std::string& steps_pref_value,
            double initial_brightness_percent);

  // Should be called when the power source changes.
  void HandlePowerSourceChange(PowerSource source);

  // system::AmbientLightObserver implementation:
  void OnAmbientLightUpdated(
      system::AmbientLightSensorInterface* sensor) override;

 private:
  // Contains information from prefs about a brightness step.
  struct BrightnessStep {
    // Backlight brightness in the range [0.0, 100.0] that corresponds to
    // this step.
    double ac_target_percent;
    double battery_target_percent;

    // If the lux level reported by |sensor_| drops below this value, a
    // lower step should be used.  -1 represents negative infinity.
    int decrease_lux_threshold;

    // If the lux level reported by |sensor_| increases above this value, a
    // higher step should be used.  -1 represents positive infinity.
    int increase_lux_threshold;
  };

  enum HysteresisState {
    // The most-recent lux level matched |lux_level_|.
    HYSTERESIS_STABLE,
    // The most-recent lux level was less than |lux_level_|.
    HYSTERESIS_DECREASING,
    // The most-recent lux level was greater than |lux_level_|.
    HYSTERESIS_INCREASING,
    // The brightness should be adjusted immediately after the next sensor
    // reading.
    HYSTERESIS_IMMEDIATE,
  };

  // Returns the current target backlight brightness percent based on
  // |step_index_| and |power_source_|.
  double GetTargetPercent() const;

  system::AmbientLightSensorInterface* sensor_;  // weak
  Delegate* delegate_;  // weak

  PowerSource power_source_;

  // Value from |sensor_| at the time of the last brightness adjustment.
  int lux_level_;

  HysteresisState hysteresis_state_;

  // If |hysteresis_state_| is HYSTERESIS_DECREASING or
  // HYSTERESIS_INCREASING, number of readings that have been received in
  // the current state.
  int hysteresis_count_;

  // Brightness step data read from prefs. It is assumed that this data is
  // well-formed; specifically, for each entry in the file, the decrease
  // thresholds are monotonically increasing and the increase thresholds
  // are monotonically decreasing.
  std::vector<BrightnessStep> steps_;

  // Current brightness step within |steps_|.
  size_t step_index_;

  // Has |delegate_| been notified about an ambient-light-triggered change
  // yet?
  bool sent_initial_adjustment_;

  // Human-readable name included in logging messages.  Useful for
  // distinguishing between different AmbientLightHandler instances.
  std::string name_;

  DISALLOW_COPY_AND_ASSIGN(AmbientLightHandler);
};

}  // namespace policy
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_POLICY_AMBIENT_LIGHT_HANDLER_H_
