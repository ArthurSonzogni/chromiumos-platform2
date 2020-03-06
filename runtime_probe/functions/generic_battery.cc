// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime_probe/functions/generic_battery.h"

#include <string>
#include <utility>
#include <vector>

#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/json/json_writer.h>
#include <base/strings/string_number_conversions.h>
#include <base/values.h>
#include <pcrecpp.h>

#include "runtime_probe/utils/file_utils.h"

namespace runtime_probe {

GenericBattery::DataType GenericBattery::Eval() const {
  DataType result{};
  auto json_output = InvokeHelperToJSON();
  if (!json_output) {
    LOG(ERROR) << "Failed to invoke helper to retrieve battery sysfs results.";
    return result;
  }
  auto battery_results = base::ListValue::From(std::move(json_output));

  for (int i = 0; i < battery_results->GetSize(); ++i) {
    base::DictionaryValue* battery_res;
    battery_results->GetDictionary(i, &battery_res);
    result.push_back(std::move(*battery_res));
  }
  return result;
}
int GenericBattery::EvalInHelper(std::string* output) const {
  constexpr char kSysfsBatteryPath[] = "/sys/class/power_supply/BAT*";
  constexpr char kSysfsExpectedType[] = "Battery";
  const std::vector<std::string> keys{"manufacturer", "model_name",
                                      "technology", "type"};
  const std::vector<std::string> optional_keys{
      "capacity",           "capacity_level",
      "charge_full",        "charge_full_design",
      "charge_now",         "current_now",
      "cycle_count",        "present",
      "serial_number",      "status",
      "voltage_min_design", "voltage_now"};

  base::ListValue result;

  const base::FilePath glob_path{kSysfsBatteryPath};
  const auto glob_root = glob_path.DirName();
  const auto glob_pattern = glob_path.BaseName();

  base::FileEnumerator battery_it(glob_root, false,
                                  base::FileEnumerator::FileType::DIRECTORIES,
                                  glob_pattern.value());
  while (true) {
    // TODO(itspeter): Extra take care if there are multiple batteries.
    auto battery_path = battery_it.Next();
    if (battery_path.empty())
      break;

    auto dict_value = MapFilesToDict(battery_path, keys, optional_keys);
    if (!dict_value.empty()) {
      std::string power_supply_type;
      if (dict_value.GetString("type", &power_supply_type) &&
          power_supply_type != kSysfsExpectedType) {
        LOG(ERROR) << "power_supply_type [" << power_supply_type << "] is not ["
                   << kSysfsExpectedType << "] for " << battery_path.value();
        continue;
      }
      dict_value.SetString("path", battery_path.value());

      pcrecpp::RE re(R"(BAT(\d+)$)", pcrecpp::RE_Options());
      int32_t battery_index;
      if (!re.PartialMatch(battery_path.value(), &battery_index)) {
        VLOG(1) << "Can't extract index from " << battery_path.value();
      } else {
        // The extracted index starts from 0. Shift it to start from 1.
        dict_value.SetString("index", base::IntToString(battery_index + 1));
      }

      result.Append(dict_value.CreateDeepCopy());
    }
  }

  if (result.GetSize() > 1) {
    LOG(ERROR) << "Multiple batteries is not supported yet.";
    return -1;
  }
  if (!base::JSONWriter::Write(result, output)) {
    LOG(ERROR)
        << "Failed to serialize generic battery probed result to json string";
    return -1;
  }
  return 0;
}

}  // namespace runtime_probe
