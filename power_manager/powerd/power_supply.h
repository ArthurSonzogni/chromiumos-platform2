// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_POWER_SUPPLY_H_
#define POWER_MANAGER_POWERD_POWER_SUPPLY_H_

#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#include <string>

#include "base/basictypes.h"
#include "base/file_path.h"
#include "base/time.h"

namespace power_manager {

class PrefsInterface;

enum BatteryState {
  BATTERY_STATE_UNKNOWN,
  BATTERY_STATE_CHARGING,
  BATTERY_STATE_DISCHARGING,
  BATTERY_STATE_EMPTY,
  BATTERY_STATE_FULLY_CHARGED
};

// Structures used for passing power supply info.
struct PowerStatus {
  bool line_power_on;

  // Amount of energy, measured in Wh, in the battery.
  double battery_energy;

  // Amount of energy being drained from the battery, measured in W. If
  // positive, the source is being discharged, if negative it's being charged.
  double battery_energy_rate;

  // Current battery levels.
  double battery_voltage;  // in volts.
  double battery_current;  // in amperes.
  double battery_charge;  // in ampere-hours.

  // Battery full charge level in ampere-hours.
  double battery_charge_full;
  // Battery full design charge level in ampere-hours.
  double battery_charge_full_design;

  // The battery voltage used in calculating time remaining.  This may or may
  // not be the same as the instantaneous voltage |battery_voltage|, as voltage
  // levels vary over the time the battery is charged or discharged.
  double nominal_voltage;

  // Set to true when we have just transitioned states and we might have both a
  // segment of charging and discharging in the calculation. This is done to
  // signal that the time value maybe inaccurate.
  bool is_calculating_battery_time;

  // Time in seconds until the battery is considered empty, 0 for unknown.
  int64 battery_time_to_empty;
  // Time in seconds until the battery is considered full. 0 for unknown.
  int64 battery_time_to_full;

  // Averaged time in seconds until the battery is considered empty, 0 for
  // unknown.
  int64 averaged_battery_time_to_empty;
  // Average time in seconds until the battery is considered full. 0 for
  // unknown.
  int64 averaged_battery_time_to_full;

  double battery_percentage;
  bool battery_is_present;

  BatteryState battery_state;
};

struct PowerInformation {
  PowerStatus power_status;

  // Amount of energy, measured in Wh, in the battery when it's considered
  // empty.
  double battery_energy_empty;

  // Amount of energy, measured in Wh, in the battery when it's considered full.
  double battery_energy_full;

  // Amount of energy, measured in Wh, the battery is designed to hold when it's
  // considered full.
  double battery_energy_full_design;

  std::string battery_vendor;
  std::string battery_model;
  std::string battery_serial;
  std::string battery_technology;

  std::string battery_state_string;
};

// Used to read power supply status from sysfs, e.g. whether on AC or battery,
// charge and voltage level, current, etc.
class PowerSupply {
 public:
  explicit PowerSupply(const base::FilePath& power_supply_path,
                       PrefsInterface *prefs);
  ~PowerSupply();

  void Init();

  // Read data from power supply sysfs and fill out all fields of the
  // PowerStatus structure if possible.
  bool GetPowerStatus(PowerStatus* status, bool is_calculating);

  // Read data from power supply sysfs for PowerInformation structure.
  bool GetPowerInformation(PowerInformation* info);

  const base::FilePath& line_power_path() const { return line_power_path_; }
  const base::FilePath& battery_path() const { return battery_path_; }

  void SetSuspendState(bool state);

 private:
  friend class PowerSupplyTest;
  FRIEND_TEST(PowerSupplyTest, TestDischargingWithHysteresis);
  FRIEND_TEST(PowerSupplyTest, TestDischargingWithSuspendResume);

  // Find sysfs directories to read from.
  void GetPowerSupplyPaths();

  // Computes time remaining based on energy drain rate.
  double GetLinearTimeToEmpty(const PowerStatus& status);

  // Determine remaining time when charging or discharging.
  void CalculateRemainingTime(PowerStatus* status);

  // Offsets the timestamps used in hysteresis calculations.  This is used when
  // suspending and resuming -- the time while suspended should not count toward
  // the hysteresis times.
  void AdjustHysteresisTimes(const base::TimeDelta& offset);

  // Used to read power supply-related prefs.
  PrefsInterface* prefs_;

  // Paths to power supply base sysfs directory and battery and line power
  // subdirectories.
  base::FilePath power_supply_path_;
  base::FilePath line_power_path_;
  base::FilePath battery_path_;

  // These are used for using hysteresis to avoid large swings in calculated
  // remaining battery time.
  double acceptable_variance_;
  base::TimeDelta hysteresis_time_;
  bool found_acceptable_time_range_;
  double acceptable_time_;
  base::TimeDelta time_outside_of_acceptable_range_;
  base::TimeTicks last_acceptable_range_time_;
  base::TimeTicks last_poll_time_;
  base::TimeTicks discharge_start_time_;
  // Use a function pointer to get the current time.  This way
  // base::TimeTicks::Now() can be mocked out by inserting an alternate
  // function.
  base::TimeTicks (*time_now_func)();

  base::TimeTicks suspend_time_;
  bool is_suspended_;

  // The fraction of full charge at which the battery can be considered "full"
  // if there is no more charging current.  Should be in the range (0, 100].
  double full_factor_;

  DISALLOW_COPY_AND_ASSIGN(PowerSupply);
};

}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_POWER_SUPPLY_H_
