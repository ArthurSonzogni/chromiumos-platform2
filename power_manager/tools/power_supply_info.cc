// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gflags/gflags.h>
#include <inttypes.h>

#include <iomanip>
#include <iostream>
#include <string>

#include "base/basictypes.h"
#include "base/file_path.h"
#include "base/file_util.h"
#include "base/logging.h"
#include "base/string_util.h"
#include "base/time.h"
#include "power_manager/common/power_constants.h"
#include "power_manager/common/prefs.h"
#include "power_manager/powerd/system/power_supply.h"

// Displays info about battery and line power.

using base::TimeDelta;

DEFINE_string(prefs_dir, "/var/lib/power_manager",
              "Directory containing prefs that can be changed at runtime.");
DEFINE_string(default_prefs_dir, "/usr/share/power_manager",
              "Directory containing default prefs.");

namespace {

// Path to power supply info.
const char kPowerStatusPath[] = "/sys/class/power_supply";

std::string BoolToString(bool value) {
  return value ? "yes" : "no";
}

template <class T>
std::string ValueToString(T value) {
  std::stringstream stream;
  stream << value;
  return stream.str();
}

std::string SecondsToString(int64 time_in_seconds) {
  // Calculate hours, minutes, and seconds, then display in H:M:S format.
  TimeDelta time = TimeDelta::FromSeconds(time_in_seconds);
  int hours = time.InHours();
  time -= TimeDelta::FromHours(hours);
  int minutes = time.InMinutes();
  time -= TimeDelta::FromMinutes(minutes);
  int64 seconds = time.InSeconds();

  std::stringstream stream;
  stream << std::setfill('0');
  stream << hours << ":"
         << std::setw(2) << minutes << ":"
         << std::setw(2) << seconds;
  return stream.str();
}

class InfoDisplay {
 public:
  InfoDisplay() : name_indent_(0), value_indent_(0) {}

  void SetIndent(int name_indent, int value_indent) {
    name_indent_ = name_indent;
    value_indent_ = value_indent;
  }

  void PrintStringValue(const std::string& name_field,
                        const std::string& value_field) {
    std::cout << std::setw(name_indent_)
              << ""
              << std::setw(value_indent_ - name_indent_)
              << std::setiosflags(std::ios::left)
              << std::resetiosflags(std::ios::right)
              << name_field + ":"
              << value_field
              << std::endl;
  }

  template <class T>
  void PrintValue(const std::string& name_field, T value) {
    PrintStringValue(name_field, ValueToString(value));
  }

  void PrintString(const std::string& string) {
    std::cout << std::setw(name_indent_)
              << ""
              << string
              << std::endl;
  }

 private:
  int name_indent_;
  int value_indent_;

  DISALLOW_COPY_AND_ASSIGN(InfoDisplay);
};

}  // namespace

int main(int argc, char** argv) {
  google::ParseCommandLineFlags(&argc, &argv, true);

  std::vector<base::FilePath> pref_paths;
  pref_paths.push_back(base::FilePath(FLAGS_prefs_dir));
  pref_paths.push_back(base::FilePath(FLAGS_default_prefs_dir));
  power_manager::Prefs prefs;
  CHECK(prefs.Init(pref_paths));

  base::FilePath path(kPowerStatusPath);
  power_manager::system::PowerSupply power_supply(path, &prefs);
  power_supply.Init();

  power_manager::system::PowerInformation power_info;
  power_manager::system::PowerStatus& power_status = power_info.power_status;
  power_supply.GetPowerInformation(&power_info);

  InfoDisplay display;
  display.SetIndent(0, 0);
  display.PrintString("Device: Line Power");
  display.SetIndent(2, 20);
  display.PrintValue("path", power_info.line_power_path);
  display.PrintStringValue("online",
                           BoolToString(power_status.line_power_on));
  display.PrintStringValue("type", power_status.line_power_type);
  switch (power_status.external_power) {
    case power_manager::PowerSupplyProperties_ExternalPower_AC:
      display.PrintStringValue("enum type", "AC");
      break;
    case power_manager::PowerSupplyProperties_ExternalPower_USB:
      display.PrintStringValue("enum type", "USB");
      break;
    case power_manager::PowerSupplyProperties_ExternalPower_DISCONNECTED:
      display.PrintStringValue("enum type", "Disconnected");
      break;
    default:
      display.PrintStringValue("enum type", "Unknown");
  }
  display.PrintValue("voltage (V)", power_status.line_power_voltage);
  display.PrintValue("current (A)", power_status.line_power_current);

  if (power_status.battery_is_present) {
    display.SetIndent(0, 0);
    display.PrintString("Device: Battery");
    display.SetIndent(2, 20);
    display.PrintValue("path", power_info.battery_path);
    display.PrintStringValue("vendor", power_info.battery_vendor);
    display.PrintStringValue("model", power_info.battery_model);
    display.PrintStringValue("serial number", power_info.battery_serial);
    display.PrintStringValue("present",
                             BoolToString(power_status.battery_is_present));

    switch (power_status.battery_state) {
      case power_manager::PowerSupplyProperties_BatteryState_FULL:
        display.PrintStringValue("state", "Fully charged");
        break;
      case power_manager::PowerSupplyProperties_BatteryState_CHARGING:
        display.PrintStringValue("state", "Charging");
        break;
      case power_manager::PowerSupplyProperties_BatteryState_DISCHARGING:
        display.PrintStringValue("state", "Discharging");
        break;
      case power_manager::PowerSupplyProperties_BatteryState_NOT_PRESENT:
        display.PrintStringValue("state", "Not present");
        break;
      default:
        display.PrintStringValue("state", "Unknown");
    }

    display.PrintValue("voltage (V)", power_status.battery_voltage);
    display.PrintValue("energy (Wh)", power_status.battery_energy);
    display.PrintValue("energy rate (W)", power_status.battery_energy_rate);
    display.PrintValue("current (A)", power_status.battery_current);
    display.PrintValue("charge (Ah)", power_status.battery_charge);
    display.PrintValue("full charge (Ah)", power_status.battery_charge_full);
    if (power_status.line_power_on)
      display.PrintValue("time to full",
                         SecondsToString(power_status.battery_time_to_full));
    else
      display.PrintValue("time to empty",
                         SecondsToString(power_status.battery_time_to_empty));
    display.PrintValue("percentage", power_status.battery_percentage);
    display.PrintStringValue("technology", power_info.battery_technology);
  }
  return 0;
}
