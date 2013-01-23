// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/power_supply.h"

#include <cmath>

#include "base/file_util.h"
#include "base/logging.h"
#include "base/string_number_conversions.h"
#include "base/string_util.h"
#include "power_manager/common/power_constants.h"
#include "power_manager/common/prefs.h"

namespace {

// For passing string pointers when no data is available, and we don't want to
// pass a NULL pointer.
const char kUnknownString[] = "Unknown";

// sysfs reports only integer values.  For non-integral values, it scales them
// up by 10^6.  This factor scales them back down accordingly.
const double kDoubleScaleFactor = 0.000001;

// How much the remaining time can vary, as a fraction of the baseline time.
const double kAcceptableVariance = 0.02;

// Initially, allow 10 seconds before deciding on an acceptable time.
const base::TimeDelta kHysteresisTimeFast = base::TimeDelta::FromSeconds(10);

// Allow three minutes before deciding on a new acceptable time.
const base::TimeDelta kHysteresisTime = base::TimeDelta::FromMinutes(3);

// Report batteries as full if they're at or above this level (out of a max of
// 1.0).
const double kDefaultFullFactor = 0.98;

// Converts time from hours to seconds.
inline double HoursToSecondsDouble(double num_hours) {
  return num_hours * 3600.;
}
// Same as above, but rounds to nearest integer.
inline int64 HoursToSecondsInt(double num_hours) {
  return lround(HoursToSecondsDouble(num_hours));
}

// Reads the contents of |filename| within |directory| into |out|, trimming
// trailing whitespace.  Returns true on success.
bool ReadAndTrimString(const FilePath& directory,
                       const std::string& filename,
                       std::string* out) {
  if (!file_util::ReadFileToString(directory.Append(filename), out))
    return false;

  TrimWhitespaceASCII(*out, TRIM_TRAILING, out);
  return true;
}

// Reads a 64-bit integer value from a file and returns true on success.
bool ReadInt64(const FilePath& directory,
               const std::string& filename,
               int64* out) {
  std::string buffer;
  if (!ReadAndTrimString(directory, filename, &buffer))
    return false;
  return base::StringToInt64(buffer, out);
}

// Reads an integer value and scales it to a double (see |kDoubleScaleFactor|.
// Returns -1.0 on failure.
double ReadScaledDouble(const FilePath& directory,
                        const std::string& filename) {
  int64 value = 0;
  return ReadInt64(directory, filename, &value) ?
      kDoubleScaleFactor * value : -1.;
}

}  // namespace

namespace power_manager {

PowerSupply::PowerSupply(const FilePath& power_supply_path,
                         PrefsInterface* prefs)
    : prefs_(prefs),
      power_supply_path_(power_supply_path),
      acceptable_variance_(kAcceptableVariance),
      hysteresis_time_(kHysteresisTimeFast),
      found_acceptable_time_range_(false),
      time_now_func(base::TimeTicks::Now),
      is_suspended_(false),
      full_factor_(kDefaultFullFactor) {}

PowerSupply::~PowerSupply() {
}

void PowerSupply::Init() {
  GetPowerSupplyPaths();
  if (prefs_)
    prefs_->GetDouble(kPowerSupplyFullFactorPref, &full_factor_);
  CHECK_GT(full_factor_, 0);
  CHECK_LE(full_factor_, 1.);
}

bool PowerSupply::GetPowerStatus(PowerStatus* status, bool is_calculating) {
  CHECK(status);
  status->is_calculating_battery_time = is_calculating;

  // Look for battery path if none has been found yet.
  if (battery_path_.empty() || line_power_path_.empty())
    GetPowerSupplyPaths();
  // The line power path should have been found during initialization, so there
  // is no need to look for it again.  However, check just to make sure the path
  // is still valid.  Better safe than sorry.
  if (!file_util::PathExists(line_power_path_) &&
      !file_util::PathExists(battery_path_)) {
#ifndef IS_DESKTOP
    // A hack for situations like VMs where there is no power supply sysfs.
    LOG(INFO) << "No power supply sysfs path found, assuming line power on.";
#endif
    status->line_power_on = true;
    status->battery_is_present = false;
    return true;
  }
  int64 value;
  bool line_power_status_found = false;
  if (file_util::PathExists(line_power_path_)) {
    ReadInt64(line_power_path_, "online", &value);
    // Return the line power status.
    status->line_power_on = static_cast<bool>(value);
    line_power_status_found = true;
  }

  // If no battery was found, or if the previously found path doesn't exist
  // anymore, return true.  This is still an acceptable case since the battery
  // could be physically removed.
  if (!file_util::PathExists(battery_path_)) {
    status->battery_is_present = false;
    return true;
  }

  ReadInt64(battery_path_, "present", &value);
  status->battery_is_present = static_cast<bool>(value);
  // If there is no battery present, we can skip the rest of the readings.
  if (!status->battery_is_present) {
    // No battery but still running means AC power must be present.
    if (!line_power_status_found)
      status->line_power_on = true;
    return true;
  }

  // Attempt to determine line power status from nominal battery status.
  if (!line_power_status_found) {
    std::string battery_status_string;
    status->line_power_on = false;
    if (ReadAndTrimString(battery_path_, "status", &battery_status_string) &&
        (battery_status_string == "Charging" ||
         battery_status_string == "Fully charged")) {
      status->line_power_on = true;
    }
  }

  double battery_voltage = ReadScaledDouble(battery_path_, "voltage_now");
  status->battery_voltage = battery_voltage;

  // Attempt to determine nominal voltage for time remaining calculations.
  // The battery voltage used in calculating time remaining.  This may or may
  // not be the same as the instantaneous voltage |battery_voltage|, as voltage
  // levels vary over the time the battery is charged or discharged.
  double nominal_voltage = -1.0;
  if (file_util::PathExists(battery_path_.Append("voltage_min_design")))
    nominal_voltage = ReadScaledDouble(battery_path_, "voltage_min_design");
  else if (file_util::PathExists(battery_path_.Append("voltage_max_design")))
    nominal_voltage = ReadScaledDouble(battery_path_, "voltage_max_design");

  // Nominal voltage is not required to obtain charge level.  If it is missing,
  // just log a message, set to |battery_voltage| so time remaining
  // calculations will function, and proceed.
  if (nominal_voltage <= 0) {
    LOG(WARNING) << "Invalid voltage_min/max_design reading: "
                 << nominal_voltage << "V."
                 << " Time remaining calculations will not be available.";
    nominal_voltage = battery_voltage;
  }
  status->nominal_voltage = nominal_voltage;

  // ACPI has two different battery types: charge_battery and energy_battery.
  // The main difference is that charge_battery type exposes
  // 1. current_now in A
  // 2. charge_{now, full, full_design} in Ah
  // while energy_battery type exposes
  // 1. power_now W
  // 2. energy_{now, full, full_design} in Wh
  // Change all the energy readings to charge format.
  // If both energy and charge reading are present (some non-ACPI drivers
  // expose both readings), read only the charge format.
  double battery_charge_full = 0;
  double battery_charge_full_design = 0;
  double battery_charge = 0;

  if (file_util::PathExists(battery_path_.Append("charge_full"))) {
    battery_charge_full = ReadScaledDouble(battery_path_, "charge_full");
    battery_charge_full_design =
        ReadScaledDouble(battery_path_, "charge_full_design");
    battery_charge = ReadScaledDouble(battery_path_, "charge_now");
  } else if (file_util::PathExists(battery_path_.Append("energy_full"))) {
    // Valid |battery_voltage| is required to determine the charge so return
    // early if it is not present. In this case, we know nothing about
    // battery state or remaining percentage, so set proper status.
    if (battery_voltage <= 0) {
      status->battery_state = BATTERY_STATE_UNKNOWN;
      status->battery_percentage = -1;
      LOG(WARNING) << "Invalid voltage_now reading for energy-to-charge"
                   << " conversion: " << battery_voltage;
      return false;
    }
    battery_charge_full =
        ReadScaledDouble(battery_path_, "energy_full") / battery_voltage;
    battery_charge_full_design =
        ReadScaledDouble(battery_path_, "energy_full_design") / battery_voltage;
    battery_charge =
        ReadScaledDouble(battery_path_, "energy_now") / battery_voltage;
  } else {
    LOG(WARNING) << "No charge/energy readings for battery";
    return false;
  }
  status->battery_charge_full = battery_charge_full;
  status->battery_charge_full_design = battery_charge_full_design;
  status->battery_charge = battery_charge;

  // Sometimes current could be negative.  Ignore it and use |line_power_on| to
  // determine whether it's charging or discharging.
  double battery_current = 0;
  if (file_util::PathExists(battery_path_.Append("power_now"))) {
    battery_current = fabs(ReadScaledDouble(battery_path_, "power_now")) /
        battery_voltage;
  } else {
    battery_current = fabs(ReadScaledDouble(battery_path_, "current_now"));
  }
  status->battery_current = battery_current;

  // Perform calculations / interpretations of the data read from sysfs.
  status->battery_energy = battery_charge * battery_voltage;
  status->battery_energy_rate = battery_current * battery_voltage;

  CalculateRemainingTime(status);

  if (battery_charge_full > 0 && battery_charge_full_design > 0)
    status->battery_percentage =
        std::min(100., 100. * battery_charge / battery_charge_full);
  else
    status->battery_percentage = -1;

  // Determine battery state from above readings.  Disregard the "status" field
  // in sysfs, as that can be inconsistent with the numerical readings.
  status->battery_state = BATTERY_STATE_UNKNOWN;
  if (status->line_power_on) {
    if (battery_charge >= battery_charge_full ||
        (battery_charge >= battery_charge_full * full_factor_ &&
         battery_current == 0)) {
      status->battery_state = BATTERY_STATE_FULLY_CHARGED;
    } else {
      if (battery_current <= 0)
        LOG(WARNING) << "Line power is on and battery is not fully charged "
                     << "but battery current is " << battery_current << " A.";
      status->battery_state = BATTERY_STATE_CHARGING;
    }
  } else {
    status->battery_state = BATTERY_STATE_DISCHARGING;
    if (battery_charge == 0)
      status->battery_state = BATTERY_STATE_EMPTY;
  }
  return true;
}

bool PowerSupply::GetPowerInformation(PowerInformation* info) {
  CHECK(info);
  GetPowerStatus(&info->power_status, false);
  if (!info->power_status.battery_is_present)
    return true;

  info->battery_vendor.clear();
  info->battery_model.clear();
  info->battery_serial.clear();
  info->battery_technology.clear();

  // POWER_SUPPLY_PROP_VENDOR does not seem to be a valid property
  // defined in <linux/power_supply.y>.
  if (file_util::PathExists(battery_path_.Append("manufacturer")))
    ReadAndTrimString(battery_path_, "manufacturer", &info->battery_vendor);
  else
    ReadAndTrimString(battery_path_, "vendor", &info->battery_vendor);
  ReadAndTrimString(battery_path_, "model_name", &info->battery_model);
  ReadAndTrimString(battery_path_, "serial_number", &info->battery_serial);
  ReadAndTrimString(battery_path_, "technology", &info->battery_technology);

  switch (info->power_status.battery_state) {
    case BATTERY_STATE_CHARGING:
      info->battery_state_string = "Charging";
      break;
    case BATTERY_STATE_DISCHARGING:
      info->battery_state_string = "Discharging";
      break;
    case BATTERY_STATE_EMPTY:
      info->battery_state_string = "Empty";
      break;
    case BATTERY_STATE_FULLY_CHARGED:
      info->battery_state_string = "Fully charged";
      break;
    default:
      info->battery_state_string = "Unknown";
      break;
  }
  return true;
}

void PowerSupply::SetSuspendState(bool state) {
  // Do not take any action if there is no change in suspend state.
  if (is_suspended_ == state)
    return;
  is_suspended_ = state;

  // Record the suspend time.
  if (is_suspended_) {
    suspend_time_ = time_now_func();
    return;
  }

  // If resuming, deduct the time suspended from the hysteresis state machine
  // timestamps.
  base::TimeDelta offset = time_now_func() - suspend_time_;
  AdjustHysteresisTimes(offset);
}

void PowerSupply::GetPowerSupplyPaths() {
  // First check if both line power and battery paths have been found and still
  // exist.  If so, there is no need to do anything else.
  if (file_util::PathExists(battery_path_) &&
      file_util::PathExists(line_power_path_))
    return;
  // Use a FileEnumerator to browse through all files/subdirectories in the
  // power supply sysfs directory.
  file_util::FileEnumerator file_enum(power_supply_path_, false,
      file_util::FileEnumerator::DIRECTORIES);
  // Read type info from all power sources, and try to identify battery and line
  // power sources.  Their paths are to be stored locally.
  for (FilePath path = file_enum.Next();
       !path.empty();
       path = file_enum.Next()) {
    std::string buf;
    if (file_util::ReadFileToString(path.Append("type"), &buf)) {
      TrimWhitespaceASCII(buf, TRIM_TRAILING, &buf);
      // Only look for battery / line power paths if they haven't been found
      // already.  This makes the assumption that they don't change (but battery
      // path can disappear if removed).  So this code should only be run once
      // for each power source.
      if (buf == "Battery" && battery_path_.empty()) {
        DLOG(INFO) << "Battery path found: " << path.value();
        battery_path_ = path;
      } else if (buf == "Mains" && line_power_path_.empty()) {
        DLOG(INFO) << "Line power path found: " << path.value();
        line_power_path_ = path;
      }
    }
  }
}

double PowerSupply::GetLinearTimeToEmpty(const PowerStatus& status) {
  return HoursToSecondsDouble(status.nominal_voltage * status.battery_charge /
      (status.battery_current * status.battery_voltage));
}

void PowerSupply::CalculateRemainingTime(PowerStatus* status) {
  CHECK(time_now_func);
  base::TimeTicks time_now = time_now_func();
  // This function might be called due to a race condition between the suspend
  // process and the battery polling.  If that's the case, handle it gracefully
  // by updating the hysteresis times and suspend time.
  //
  // Since the time between suspend and now has been taken into account in the
  // hysteresis times, the recorded suspend time should be updated to the
  // current time, to compensate.
  //
  // Example:
  // Hysteresis time = 3
  // At time t=0, there is a read of the power supply.
  // At time t=1, the system is suspended.
  // At time t=4, the system is resumed.  There is a power supply read at t=4.
  // At time t=4.5, SetSuspendState(false) is called (latency in resume process)
  //
  // At t=4, the remaining time could be set to something very high, based on
  // the low suspend current, since the time since last read is greater than the
  // hysteresis time.
  //
  // The solution is to shift the last read time forward by 3, which is the time
  // elapsed between suspend (t=1) and the next reading (t=4).  Thus, the time
  // of last read becomes t=3, and time since last read becomes 1 instead of 4.
  // This avoids triggering the time hysteresis adjustment.
  //
  // At this point, the suspend time is also reset to the current time.  This is
  // so that when AdjustHysteresisTimes() is called again (e.g. during resume),
  // the previous period of t=1 to t=4 is not used again in the adjustment.
  // Continuing the example:
  // At t=4.5, SetSuspendState(false) is called, and it calls
  //   AdjustHysteresisTimes().  Since suspend time has been adjusted from t=1
  //   to t=4, the new offset is only 0.5.  So time of last read gets shifted
  //   from t=3 to t=3.5.
  // If suspend time was not reset to t=4, then we'd have an offset of 3.5
  // instead of 0.5, and time of last read gets set from t=3 to t=6.5, which is
  // invalid.
  if (is_suspended_) {
    AdjustHysteresisTimes(time_now - suspend_time_);
    suspend_time_ = time_now;
  }

  // Check to make sure there isn't a division by zero.
  if (status->battery_current > 0) {
    double time_to_empty = 0;
    if (status->line_power_on) {
      status->battery_time_to_full =
          HoursToSecondsInt((status->battery_charge_full -
              status->battery_charge) / status->battery_current);
      // Reset the remaining-time-calculation state machine when AC plugged in.
      found_acceptable_time_range_ = false;
      last_poll_time_ = base::TimeTicks();
      discharge_start_time_ = base::TimeTicks();
      last_acceptable_range_time_ = base::TimeTicks();
      // Make sure that when the system switches to battery power, the initial
      // hysteresis time will be very short, so it can find an acceptable
      // battery remaining time as quickly as possible.
      hysteresis_time_ = kHysteresisTimeFast;
    } else if (!found_acceptable_time_range_) {
      // No base range found, need to give it some time to stabilize.  For now,
      // use the simple linear calculation for time.
      if (discharge_start_time_.is_null())
        discharge_start_time_ = time_now;
      time_to_empty = GetLinearTimeToEmpty(*status);
      status->battery_time_to_empty = lround(time_to_empty);
      // Select an acceptable remaining time once the system has been
      // discharging for the necessary amount of time.
      if (time_now - discharge_start_time_ >= hysteresis_time_) {
        acceptable_time_ = time_to_empty;
        found_acceptable_time_range_ = true;
        last_poll_time_ = last_acceptable_range_time_ = time_now;
        // Since an acceptable time has been found, start using the normal
        // hysteresis time going forward.
        hysteresis_time_ = kHysteresisTime;
      }
    } else {
      double calculated_time = GetLinearTimeToEmpty(*status);
      double allowed_time_variation = acceptable_time_ * acceptable_variance_;
      // Reduce the acceptable time range as time goes by.
      acceptable_time_ -= (time_now - last_poll_time_).InSecondsF();
      if (fabs(calculated_time - acceptable_time_) <= allowed_time_variation) {
        last_acceptable_range_time_ = time_now;
        time_to_empty = calculated_time;
      } else if (time_now - last_acceptable_range_time_ >= hysteresis_time_) {
        // If the calculated time has been outside the acceptable range for a
        // long enough period of time, make it the basis for a new acceptable
        // range.
        acceptable_time_ = calculated_time;
        time_to_empty = calculated_time;
        found_acceptable_time_range_ = true;
        last_acceptable_range_time_ = time_now;
      } else if (calculated_time < acceptable_time_ - allowed_time_variation) {
        // Clip remaining time at lower bound if it is too low.
        time_to_empty = acceptable_time_ - allowed_time_variation;
      } else {
        // Clip remaining time at upper bound if it is too high.
        time_to_empty = acceptable_time_ + allowed_time_variation;
      }
      last_poll_time_ = time_now;
    }
    status->battery_time_to_empty = lround(time_to_empty);
  } else {
    status->battery_time_to_empty = 0;
    status->battery_time_to_full = 0;
  }
}

void PowerSupply::AdjustHysteresisTimes(const base::TimeDelta& offset) {
  if (!discharge_start_time_.is_null())
    discharge_start_time_ += offset;
  if (!last_acceptable_range_time_.is_null())
    last_acceptable_range_time_ += offset;
  if (!last_poll_time_.is_null())
    last_poll_time_ += offset;
}

}  // namespace power_manager
