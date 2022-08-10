/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "camera/features/feature_profile.h"

#include <iomanip>
#include <optional>
#include <string>
#include <utility>

#include <base/files/file_util.h>

#include "cros-camera/common.h"
#include "cros-camera/constants.h"

namespace cros {

namespace {

std::optional<FeatureProfile::FeatureType> GetFeatureType(
    const std::string& feature_key) {
  if (feature_key == "auto_framing") {
    return FeatureProfile::FeatureType::kAutoFraming;
  } else if (feature_key == "face_detection") {
    return FeatureProfile::FeatureType::kFaceDetection;
  } else if (feature_key == "gcam_ae") {
    return FeatureProfile::FeatureType::kGcamAe;
  } else if (feature_key == "hdrnet") {
    return FeatureProfile::FeatureType::kHdrnet;
  }
  return std::nullopt;
}

}  // namespace

FeatureProfile::FeatureProfile(std::optional<base::Value> feature_config,
                               std::optional<DeviceConfig> device_config)
    : config_file_(ReloadableConfigFile::Options{
          base::FilePath(kFeatureProfileFilePath), base::FilePath()}),
      device_config_(device_config ? std::move(device_config).value()
                                   : DeviceConfig::Create()) {
  if (feature_config.has_value()) {
    OnOptionsUpdated(feature_config.value());
  } else {
    config_file_.SetCallback(base::BindRepeating(
        &FeatureProfile::OnOptionsUpdated, base::Unretained(this)));
  }
}

bool FeatureProfile::IsEnabled(FeatureType feature) const {
  switch (feature) {
    case FeatureType::kHdrnet:
    case FeatureType::kGcamAe:
      if (base::PathExists(
              base::FilePath(constants::kForceDisableHdrNetPath))) {
        return false;
      }
      if (base::PathExists(base::FilePath(constants::kForceEnableHdrNetPath))) {
        return true;
      }
      break;
    case FeatureType::kAutoFraming:
      if (base::PathExists(
              base::FilePath(constants::kForceDisableAutoFramingPath))) {
        return false;
      }
      if (base::PathExists(
              base::FilePath(constants::kForceEnableAutoFramingPath))) {
        return true;
      }
      break;
    default:
      break;
  }
  return feature_settings_.contains(feature);
}

base::FilePath FeatureProfile::GetConfigFilePath(FeatureType feature) const {
  auto setting = feature_settings_.find(feature);
  if (setting == feature_settings_.end()) {
    return base::FilePath();
  }
  return setting->second.config_file_path;
}

void FeatureProfile::OnOptionsUpdated(const base::Value& json_values) {
  // Feature config file schema:
  //
  // {
  //   <model>: {
  //     "feature_set": [
  //       {"type": <feature_type>, "config_file_path": <config_file_path>},
  //       ...
  //     ]
  //   },
  //   ...
  // }
  //
  // <model>: String of device model name, e.g. "redrix".
  // <feature_type>: String for the type of the feature, e.g. "face_detection"
  //                 or "hdrnet".
  // <config_file_path>: String specifying the path to the feature config file.

  constexpr char kKeyFeatureSet[] = "feature_set";
  constexpr char kKeyType[] = "type";
  constexpr char kKeyConfigFilePath[] = "config_file_path";

  if (!device_config_.has_value()) {
    LOGF(WARNING) << "Device config is invalid, cannot determine model name";
    return;
  }

  if (!json_values.is_dict()) {
    LOGF(ERROR) << "Feature config must be a dict";
    return;
  }

  // Get the per-model feature profile from the top-level.
  const base::Value* feature_profile =
      json_values.FindDictKey(device_config_->GetModelName());
  if (feature_profile == nullptr) {
    LOGF(ERROR) << "Cannot find feature profile as dict for device model "
                << std::quoted(device_config_->GetModelName());
    return;
  }

  // Extract "feature_set" info from the feature profile.
  const base::Value* feature_set = feature_profile->FindListKey(kKeyFeatureSet);
  if (feature_set == nullptr) {
    LOGF(ERROR) << "Cannot find " << std::quoted(kKeyFeatureSet)
                << " as list in the feature profile of "
                << std::quoted(device_config_->GetModelName());
    return;
  }

  // Construct the complete feature settings.
  for (const auto& v : feature_set->GetList()) {
    if (!v.is_dict()) {
      LOGF(ERROR) << "Feature setting in " << std::quoted(kKeyFeatureSet)
                  << " must be a dict";
      continue;
    }
    const std::string* type_str = v.FindStringKey(kKeyType);
    if (type_str == nullptr) {
      LOGF(ERROR) << "Malformed feature setting: Cannot find key "
                  << std::quoted(kKeyType);
      continue;
    }
    std::optional<FeatureType> type = GetFeatureType(*type_str);
    if (!type.has_value()) {
      LOGF(ERROR) << "Unknown feature " << std::quoted(*type_str);
      continue;
    }
    const std::string* path_str = v.FindStringKey(kKeyConfigFilePath);
    if (type_str == nullptr) {
      LOGF(ERROR) << "Malformed feature setting: Cannot find key "
                  << std::quoted(kKeyConfigFilePath);
      continue;
    }
    feature_settings_.insert(
        {*type, {.config_file_path = base::FilePath(*path_str)}});
  }
}

}  // namespace cros
