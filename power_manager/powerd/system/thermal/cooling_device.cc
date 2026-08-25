// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/system/thermal/cooling_device.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <base/check.h>
#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <re2/re2.h>

#include "power_manager/common/util.h"
#include "power_manager/powerd/system/thermal/device_thermal_state.h"

namespace power_manager::system {

namespace {

// File name of cooling device type in sysfs.
const char kTypeFileName[] = "type";

// File name of cooling device maximum state in sysfs.
const char kMaxStateFileName[] = "max_state";

// File name of cooling device current state in sysfs.
const char kCurStateFileName[] = "cur_state";

// Fan and charger .critical = 2.0 indicates no critical state for that type.
const std::unordered_map<ThermalDeviceType, CoolingStateScale> kScale = {
    {ThermalDeviceType::kProcessorCooling, kDefaultSocCoolingScale},
    {ThermalDeviceType::kFanCooling,
     {.fair = 0.5, .serious = 1.0, .critical = 2.0}},
    {ThermalDeviceType::kChargerCooling,
     {.fair = 0.7, .serious = 1.0, .critical = 2.0}},
    {ThermalDeviceType::kOtherCooling,
     {.fair = 0.5, .serious = 0.8, .critical = 1.0}},
    {ThermalDeviceType::kSocCooling, kDefaultSocCoolingScale},
};

}  // namespace

bool CoolingDevice::InitSysfsFile() {
  if (!base::PathExists(device_path_)) {
    LOG(ERROR) << "Nonexistent path: " << device_path_;
    return false;
  }

  base::FilePath max_state_path = device_path_.Append(kMaxStateFileName);
  int64_t max_state;
  if (!util::ReadInt64File(max_state_path, &max_state)) {
    return false;
  }

  if (max_state == 0) {
    LOG(INFO) << "Ignore max_state = 0 cooling device: " << device_path_;
    return false;
  }

  base::FilePath type_path = device_path_.Append(kTypeFileName);
  std::string type;
  if (!util::ReadStringFile(type_path, &type)) {
    type = "Unknown";
  }
  type = base::TrimWhitespaceASCII(type, base::TRIM_ALL);

  base::FilePath cur_state_path = device_path_.Append(kCurStateFileName);
  int64_t cur_state;
  if (!util::ReadInt64File(cur_state_path, &cur_state)) {
    return false;
  }

  // DCHECK since it would be an unlikely kernel bug if this happened.
  DCHECK(cur_state <= max_state);

  max_state_ = max_state;
  polling_path_ = cur_state_path;
  static constexpr LazyRE2 kFanRegex = {"(?i)tfn|fan|fn"};
  static constexpr LazyRE2 kProcessorRegex = {"(?i)processor"};
  static constexpr LazyRE2 kChargerRegex = {"(?i)charge|chg"};
  static constexpr LazyRE2 kSocCoolingRegex = {
      "(?i)(?:thermal-)?(?:cpu|cpufreq|devfreq)"};

  if (re2::RE2::PartialMatch(type, *kFanRegex)) {
    type_ = ThermalDeviceType::kFanCooling;
  } else if (re2::RE2::PartialMatch(type, *kProcessorRegex)) {
    type_ = ThermalDeviceType::kProcessorCooling;
  } else if (re2::RE2::PartialMatch(type, *kChargerRegex)) {
    type_ = ThermalDeviceType::kChargerCooling;
  } else if (re2::RE2::PartialMatch(type, *kSocCoolingRegex)) {
    type_ = ThermalDeviceType::kSocCooling;
    InitSocWeight(type);
  } else {
    type_ = ThermalDeviceType::kOtherCooling;
  }
  threshold_fair_ =
      ceil(static_cast<double>(max_state) * kScale.at(type_).fair);
  threshold_serious_ =
      ceil(static_cast<double>(max_state) * kScale.at(type_).serious);
  threshold_critical_ =
      ceil(static_cast<double>(max_state) * kScale.at(type_).critical);

  LOG(INFO) << "Cooling device " << device_path_ << " type=" << type
            << " initialized with max_state=" << max_state_
            << ", weight=" << weight_
            << ", thresholds: fair=" << threshold_fair_
            << ", serious=" << threshold_serious_
            << ", critical=" << threshold_critical_;

  polling_file_.Init(polling_path_);
  return true;
}

std::optional<double> CoolingDevice::FindDtsThermalWeight() const {
  base::FilePath thermal_dir = device_path_.DirName();
  base::FileEnumerator tz_enumerator(thermal_dir, false,
                                     base::FileEnumerator::FILES |
                                         base::FileEnumerator::DIRECTORIES |
                                         base::FileEnumerator::SHOW_SYM_LINKS,
                                     "thermal_zone*");
  base::FilePath canonical_device;
  bool has_canonical_device =
      base::NormalizeFilePath(device_path_, &canonical_device);
  static constexpr LazyRE2 kCdevLinkRegex = {"cdev[0-9]+"};
  for (base::FilePath tz_path = tz_enumerator.Next(); !tz_path.empty();
       tz_path = tz_enumerator.Next()) {
    base::FileEnumerator cdev_enumerator(
        tz_path, false,
        base::FileEnumerator::FILES | base::FileEnumerator::DIRECTORIES |
            base::FileEnumerator::SHOW_SYM_LINKS,
        "cdev*");
    for (base::FilePath cdev_path = cdev_enumerator.Next(); !cdev_path.empty();
         cdev_path = cdev_enumerator.Next()) {
      std::string cdev_name = cdev_path.BaseName().value();
      if (!re2::RE2::FullMatch(cdev_name, *kCdevLinkRegex)) {
        continue;
      }
      base::FilePath target_link;
      if (!base::ReadSymbolicLink(cdev_path, &target_link)) {
        continue;
      }
      base::FilePath abs_target =
          target_link.IsAbsolute() ? target_link : tz_path.Append(target_link);
      base::FilePath canonical_target;
      bool is_match = false;
      if (has_canonical_device &&
          base::NormalizeFilePath(abs_target, &canonical_target)) {
        is_match = (canonical_target == canonical_device);
      } else {
        is_match = (abs_target.BaseName() == device_path_.BaseName());
      }
      if (!is_match) {
        continue;
      }
      base::FilePath weight_path = tz_path.Append(cdev_name + "_weight");
      int64_t weight_val = 0;
      // Note: On platforms without explicit thermal governor weights in
      // device-tree (e.g. Qualcomm SC7180 / Trogdor), the kernel creates
      // cdev*_weight sysfs files with a default value of 0. We treat 0 as
      // "no weight specified" and fall through to EAS capacity calculation.
      if (util::ReadInt64File(weight_path, &weight_val) && weight_val > 0) {
        // Note: Current ChromeOS devices only have 1 thermal zone with
        // cooling device weights defined in device-tree. If a device is
        // bound to multiple thermal zones in the future, we pick the weight
        // from the first matching zone (weights are configured consistently
        // in device-tree).
        LOG(INFO) << "Found DTS thermal weight " << weight_val << " for "
                  << device_path_ << " in " << tz_path.BaseName();
        return static_cast<double>(weight_val);
      }
    }
  }
  return std::nullopt;
}

std::optional<double> CoolingDevice::CalculateEasCpuWeight(
    const std::string& type_str) const {
  int cpu_id = -1;
  static constexpr LazyRE2 kCpuIdRegex = {
      "(?i)(?:thermal-)?(?:cpufreq|cpu)-?(?:cpu)?([0-9]+)"};
  if (!re2::RE2::PartialMatch(type_str, *kCpuIdRegex, &cpu_id)) {
    return std::nullopt;
  }

  int core_count = 1;
  base::FilePath policy_related =
      sys_cpu_dir_.Append("cpu" + base::NumberToString(cpu_id))
          .Append("cpufreq")
          .Append("related_cpus");
  std::string related_data;
  if (base::ReadFileToString(policy_related, &related_data)) {
    const auto cpus = base::SplitString(
        related_data, " ", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
    if (!cpus.empty()) {
      core_count = cpus.size();
    }
  }

  int64_t capacity = 1024;
  base::FilePath cap_path =
      sys_cpu_dir_.Append("cpu" + base::NumberToString(cpu_id))
          .Append("cpu_capacity");
  int64_t read_cap = 0;
  if (util::ReadInt64File(cap_path, &read_cap) && read_cap > 0) {
    capacity = read_cap;
  }

  double weight = static_cast<double>(core_count * capacity);
  LOG(INFO) << "Computed EAS capacity weight " << weight
            << " (cores=" << core_count << ", capacity=" << capacity << ") for "
            << device_path_;
  return weight;
}

void CoolingDevice::InitSocWeight(const std::string& type_str) {
  if (const auto weight = FindDtsThermalWeight(); weight.has_value()) {
    weight_ = *weight;
    return;
  }

  if (const auto weight = CalculateEasCpuWeight(type_str); weight.has_value()) {
    weight_ = *weight;
    return;
  }

  weight_ = 1024.0;
  LOG(INFO) << "Using default fallback weight " << weight_ << " for "
            << device_path_;
}

DeviceThermalState CoolingDevice::CalculateThermalState(int sysfs_data) {
  if (sysfs_data < 0 || sysfs_data > max_state_) {
    LOG(ERROR) << "Invalid value: " << sysfs_data << " at " << polling_path_;
    throttle_ratio_ = 0.0;
    return DeviceThermalState::kUnknown;
  }
  if (max_state_ == 0) {
    throttle_ratio_ = 0.0;
    return DeviceThermalState::kUnknown;
  }
  throttle_ratio_ = static_cast<double>(sysfs_data) / max_state_;
  DeviceThermalState new_state = DeviceThermalState::kNominal;
  if (sysfs_data >= threshold_critical_) {
    new_state = DeviceThermalState::kCritical;
  } else if (sysfs_data >= threshold_serious_) {
    new_state = DeviceThermalState::kSerious;
  } else if (sysfs_data >= threshold_fair_) {
    new_state = DeviceThermalState::kFair;
  }

  return new_state;
}

}  // namespace power_manager::system
