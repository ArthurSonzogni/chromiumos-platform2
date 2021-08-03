// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime_probe/functions/generic_battery.h"

#include <pcrecpp.h>

#include <string>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/values.h>

#include "runtime_probe/utils/file_utils.h"

namespace runtime_probe {

namespace {
constexpr auto kSysfsBatteryPath = "/sys/class/power_supply/*";
constexpr auto kSysfsExpectedType = "Battery";
// These keys are expected to present no matter what types of battery is:
const std::vector<std::string> kBatteryKeys{"manufacturer", "model_name",
                                            "technology", "type"};
// These keys are optional
const std::vector<std::string> kBatteryOptionalKeys{
    "capacity",           "capacity_level",
    "charge_full",        "charge_full_design",
    "charge_now",         "current_now",
    "cycle_count",        "present",
    "serial_number",      "status",
    "voltage_min_design", "voltage_now"};
}  // namespace

GenericBattery::DataType GenericBattery::EvalImpl() const {
  DataType result{};

  for (const auto& battery_path : Glob(kSysfsBatteryPath)) {
    // TODO(itspeter): Extra take care if there are multiple batteries.
    auto dict_value =
        MapFilesToDict(battery_path, kBatteryKeys, kBatteryOptionalKeys);
    if (dict_value) {
      auto* power_supply_type = dict_value->FindStringKey("type");
      if (!power_supply_type)
        continue;
      if (*power_supply_type != kSysfsExpectedType) {
        VLOG(3) << "power_supply_type [" << *power_supply_type << "] is not ["
                << kSysfsExpectedType << "] for " << battery_path.value();
        continue;
      }
      dict_value->SetStringKey("path", battery_path.value());

      pcrecpp::RE re(R"(BAT(\d+)$)", pcrecpp::RE_Options());
      int32_t battery_index;
      if (!re.PartialMatch(battery_path.value(), &battery_index)) {
        VLOG(3) << "Can't extract index from " << battery_path.value();
      } else {
        // The extracted index starts from 0. Shift it to start from 1.
        dict_value->SetStringKey("index",
                                 base::NumberToString(battery_index + 1));
      }

      result.push_back(std::move(*dict_value));
    }
  }

  if (result.size() > 1) {
    LOG(ERROR) << "Multiple batteries is not supported yet.";
    return {};
  }
  return result;
}

}  // namespace runtime_probe
