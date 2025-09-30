// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/common/cros_ec_prefs_source.h"

#include <fcntl.h>

#include <utility>

#include <base/files/file_util.h>
#include <base/strings/string_number_conversions.h>

#include "power_manager/common/power_constants.h"

namespace power_manager {

namespace {

CrosEcPrefsSource::EcPrefCommands CreateEcCommands() {
  CrosEcPrefsSource::EcPrefCommands ec_commands;
  base::ScopedFD ec_fd(open(ec::kCrosEcPath, O_RDWR));
  if (!ec_fd.is_valid()) {
    PLOG(ERROR) << "Failed to open " << ec::kCrosEcPath;
    return ec_commands;
  }

  auto display_soc_cmd = std::make_unique<ec::DisplayStateOfChargeCommand>();
  if (display_soc_cmd->Run(ec_fd.get())) {
    ec_commands.display_soc_command = std::move(display_soc_cmd);
  }

  auto get_min_charging_volt_cmd =
      std::make_unique<ec::GetMinChargingVoltCommand>();
  if (get_min_charging_volt_cmd->Run(ec_fd.get())) {
    ec_commands.get_min_charging_volt_command =
        std::move(get_min_charging_volt_cmd);
  }

  return ec_commands;
}

}  // namespace

CrosEcPrefsSource::CrosEcPrefsSource()
    : CrosEcPrefsSource(CreateEcCommands()) {}

CrosEcPrefsSource::CrosEcPrefsSource(EcPrefCommands ec_commands) {
  if (ec_commands.display_soc_command) {
    low_battery_shutdown_percent_ =
        ec_commands.display_soc_command->ShutdownPercentCharge();
    power_supply_full_factor_ = ec_commands.display_soc_command->FullFactor();
  }

  if (ec_commands.get_min_charging_volt_command) {
    min_charging_voltage_ = ec_commands.get_min_charging_volt_command->Get();
  }
}

// static
bool CrosEcPrefsSource::IsSupported() {
  return base::PathExists(base::FilePath(ec::kCrosEcPath));
}

std::string CrosEcPrefsSource::GetDescription() const {
  return "<cros_ec>";
}

bool CrosEcPrefsSource::ReadPrefString(const std::string& name,
                                       std::string* value_out) {
  if (low_battery_shutdown_percent_.has_value() &&
      name == kLowBatteryShutdownPercentPref) {
    *value_out = base::NumberToString(*low_battery_shutdown_percent_);
    return true;
  }
  if (power_supply_full_factor_.has_value() &&
      name == kPowerSupplyFullFactorPref) {
    *value_out = base::NumberToString(*power_supply_full_factor_);
    return true;
  }
  if (min_charging_voltage_.has_value() && name == kMinChargingVoltPref) {
    *value_out = base::NumberToString(*min_charging_voltage_);
    return true;
  }
  return false;
}

bool CrosEcPrefsSource::ReadExternalString(const std::string& path,
                                           const std::string& name,
                                           std::string* value_out) {
  return false;
}

}  // namespace power_manager
