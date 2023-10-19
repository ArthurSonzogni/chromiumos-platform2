// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/utils/cros_config_utils_impl.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_enumerator.h>
#include <base/files/file_util.h>
#include <base/json/json_reader.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/stringprintf.h>
#include <chromeos-config/libcros_config/cros_config.h>

#include "rmad/utils/cros_config_constants.h"

namespace rmad {

namespace {

constexpr char kChromeosConfigsRootPath[] =
    "/run/chromeos-config/private/v1/chromeos/configs";

constexpr int kMaxSsfcComponentTypeNum = 32;
constexpr int kMaxSsfcProbeableComponentNum = 1024;

constexpr char kTrueStr[] = "true";
constexpr char kUndefinedComponentType[] = "undefined_component_type";

// Helper functions for constructing DesignConfig.
std::optional<std::string> GetStringFromFile(const base::FilePath& path) {
  std::string data;
  if (!base::ReadFileToString(path, &data)) {
    return std::nullopt;
  }
  return data;
}

std::optional<uint32_t> GetUint32FromFile(const base::FilePath& path) {
  std::string data;
  if (!base::ReadFileToString(path, &data)) {
    return std::nullopt;
  }
  uint32_t value;
  if (!base::StringToUint(data, &value)) {
    return std::nullopt;
  }
  return value;
}

}  // namespace

CrosConfigUtilsImpl::CrosConfigUtilsImpl()
    : configs_root_path_(kChromeosConfigsRootPath) {
  cros_config_ = std::make_unique<brillo::CrosConfig>();
}

CrosConfigUtilsImpl::CrosConfigUtilsImpl(
    const base::FilePath& configs_root_path,
    std::unique_ptr<brillo::CrosConfigInterface> cros_config)
    : configs_root_path_(configs_root_path),
      cros_config_(std::move(cros_config)) {}

bool CrosConfigUtilsImpl::GetRmadConfig(RmadConfig* config) const {
  DCHECK(config);

  const base::FilePath rmad_path =
      base::FilePath(kCrosRootPath).Append(kCrosRmadPath);
  config->enabled =
      GetBooleanWithDefault(rmad_path.value(), kCrosRmadEnabledKey, false);
  config->has_cbi =
      GetBooleanWithDefault(rmad_path.value(), kCrosRmadHasCbiKey, false);
  config->ssfc = GetSsfc(rmad_path);
  config->use_legacy_custom_label = GetBooleanWithDefault(
      rmad_path.value(), kCrosRmadUseLegacyCustomLabelKey, false);

  return true;
}

bool CrosConfigUtilsImpl::GetModelName(std::string* model_name) const {
  DCHECK(model_name);

  return cros_config_->GetString(kCrosRootPath, kCrosModelNameKey, model_name);
}

bool CrosConfigUtilsImpl::GetBrandCode(std::string* brand_code) const {
  DCHECK(brand_code);

  return cros_config_->GetString(kCrosRootPath, kCrosBrandCodeKey, brand_code);
}

bool CrosConfigUtilsImpl::GetSkuId(uint32_t* sku_id) const {
  DCHECK(sku_id);

  std::string sku_id_str;
  const base::FilePath identity_path =
      base::FilePath(kCrosRootPath).Append(kCrosIdentityPath);
  if (!cros_config_->GetString(identity_path.value(), kCrosIdentitySkuKey,
                               &sku_id_str)) {
    return false;
  }

  return base::StringToUint(sku_id_str, sku_id);
}

bool CrosConfigUtilsImpl::GetCustomLabelTag(
    std::string* custom_label_tag) const {
  DCHECK(custom_label_tag);

  const base::FilePath identity_path =
      base::FilePath(kCrosRootPath).Append(kCrosIdentityPath);
  return cros_config_->GetString(
      identity_path.value(), kCrosIdentityCustomLabelTagKey, custom_label_tag);
}

bool CrosConfigUtilsImpl::GetFirmwareConfig(uint32_t* firmware_config) const {
  DCHECK(firmware_config);

  std::string firmware_config_str;
  const base::FilePath identity_path =
      base::FilePath(kCrosRootPath).Append(kCrosFirmwarePath);
  if (!cros_config_->GetString(identity_path.value(),
                               kCrosFirmwareFirmwareConfigKey,
                               &firmware_config_str)) {
    return false;
  }

  return base::StringToUint(firmware_config_str, firmware_config);
}

bool CrosConfigUtilsImpl::GetDesignConfigList(
    std::vector<DesignConfig>* design_config_list) const {
  DCHECK(design_config_list);

  std::string current_model;
  if (!GetModelName(&current_model)) {
    LOG(ERROR) << "Failed to get model name for comparison";
    return false;
  }

  design_config_list->clear();
  base::FileEnumerator e(configs_root_path_,
                         /*recursive=*/false,
                         base::FileEnumerator::FileType::DIRECTORIES);
  for (base::FilePath path = e.Next(); !path.empty(); path = e.Next()) {
    DesignConfig design_config;
    // Only return design configs under the same model.
    std::optional<std::string> model_name =
        GetStringFromFile(path.Append(kCrosModelNameKey));
    if (model_name.has_value() && model_name.value() == current_model) {
      design_config.model_name = model_name.value();
    } else {
      continue;
    }
    // SKU ID should exist on most devices, but some devices that use strapping
    // pins don't populate it.
    design_config.sku_id = GetUint32FromFile(
        path.Append(kCrosIdentityPath).Append(kCrosIdentitySkuKey));
    // Custom label tag might not exist.
    design_config.custom_label_tag = GetStringFromFile(
        path.Append(kCrosIdentityPath).Append(kCrosIdentityCustomLabelTagKey));

    design_config_list->emplace_back(std::move(design_config));
  }

  return true;
}

bool CrosConfigUtilsImpl::GetSkuIdList(
    std::vector<uint32_t>* sku_id_list) const {
  DCHECK(sku_id_list);

  // TODO(chenghan): Cache the design config list to save time.
  std::vector<DesignConfig> design_config_list;
  if (!GetDesignConfigList(&design_config_list)) {
    LOG(ERROR) << "Failed to get design config";
    return false;
  }

  // Get sorted unique list of SKU IDs.
  std::set<uint32_t> sku_id_set;
  for (const DesignConfig& design_config : design_config_list) {
    if (design_config.sku_id.has_value()) {
      sku_id_set.insert(design_config.sku_id.value());
    }
  }
  sku_id_list->clear();
  for (uint32_t sku_id : sku_id_set) {
    sku_id_list->push_back(sku_id);
  }
  sort(sku_id_list->begin(), sku_id_list->end());
  return true;
}

bool CrosConfigUtilsImpl::GetCustomLabelTagList(
    std::vector<std::string>* custom_label_tag_list) const {
  DCHECK(custom_label_tag_list);

  // TODO(chenghan): Cache the design config list to save time.
  std::vector<DesignConfig> design_config_list;
  if (!GetDesignConfigList(&design_config_list)) {
    LOG(ERROR) << "Failed to get design config";
    return false;
  }

  // Get sorted unique list of custom labels.
  std::set<std::string> custom_label_tag_set;
  for (const DesignConfig& design_config : design_config_list) {
    if (design_config.custom_label_tag.has_value()) {
      custom_label_tag_set.insert(design_config.custom_label_tag.value());
    } else {
      // Custom label tag can be empty.
      custom_label_tag_set.insert("");
    }
  }
  custom_label_tag_list->clear();
  for (const std::string& custom_label_tag : custom_label_tag_set) {
    custom_label_tag_list->push_back(custom_label_tag);
  }
  sort(custom_label_tag_list->begin(), custom_label_tag_list->end());
  return true;
}

std::string CrosConfigUtilsImpl::GetStringWithDefault(
    const std::string& path,
    const std::string& key,
    const std::string& default_value) const {
  std::string ret = default_value;
  cros_config_->GetString(path, key, &ret);
  return ret;
}

bool CrosConfigUtilsImpl::GetBooleanWithDefault(const std::string& path,
                                                const std::string& key,
                                                bool default_value) const {
  bool ret = default_value;
  if (std::string value_str; cros_config_->GetString(path, key, &value_str)) {
    ret = (value_str == kTrueStr);
  }
  return ret;
}

uint32_t CrosConfigUtilsImpl::GetUintWithDefault(const std::string& path,
                                                 const std::string& key,
                                                 uint32_t default_value) const {
  uint32_t ret = default_value;
  if (std::string value_str; cros_config_->GetString(path, key, &value_str)) {
    if (uint32_t value; base::StringToUint(value_str, &value)) {
      ret = value;
    }
  }
  return ret;
}

SsfcConfig CrosConfigUtilsImpl::GetSsfc(const base::FilePath& rmad_path) const {
  SsfcConfig ssfc;
  const base::FilePath ssfc_path = rmad_path.Append(kCrosSsfcPath);
  ssfc.mask = GetUintWithDefault(ssfc_path.value(), kCrosSsfcMaskKey, 0);
  ssfc.component_type_configs = GetSsfcComponentTypeConfigs(ssfc_path);
  // SSFC config integrity check. No component should set the bits in the mask.
  for (const auto& component_type_config : ssfc.component_type_configs) {
    for (const auto& [identifier, value] :
         component_type_config.probeable_components) {
      if (value & ssfc.mask) {
        LOG(WARNING) << "Component " << identifier << " has SSFC value "
                     << value << " which conflicts with SSFC mask "
                     << ssfc.mask;
      }
    }
  }
  return ssfc;
}

std::vector<SsfcComponentTypeConfig>
CrosConfigUtilsImpl::GetSsfcComponentTypeConfigs(
    const base::FilePath& ssfc_path) const {
  std::vector<SsfcComponentTypeConfig> component_type_configs;
  const base::FilePath component_type_configs_path =
      ssfc_path.Append(kCrosComponentTypeConfigsPath);
  for (int i = 0; i < kMaxSsfcComponentTypeNum; ++i) {
    SsfcComponentTypeConfig component_type_config = GetSsfcComponentTypeConfig(
        component_type_configs_path.Append(base::NumberToString(i)));
    if (component_type_config.probeable_components.size()) {
      component_type_configs.emplace_back(std::move(component_type_config));
    } else {
      break;
    }
  }
  return component_type_configs;
}

SsfcComponentTypeConfig CrosConfigUtilsImpl::GetSsfcComponentTypeConfig(
    const base::FilePath& component_type_config_path) const {
  SsfcComponentTypeConfig config;
  config.component_type = GetStringWithDefault(
      component_type_config_path.value(),
      kCrosComponentTypeConfigsComponentTypeKey, kUndefinedComponentType);
  config.default_value =
      GetUintWithDefault(component_type_config_path.value(),
                         kCrosComponentTypeConfigsDefaultValueKey, 0);
  config.probeable_components =
      GetSsfcProbeableComponents(component_type_config_path);
  return config;
}

std::map<std::string, uint32_t> CrosConfigUtilsImpl::GetSsfcProbeableComponents(
    const base::FilePath& component_type_config_path) const {
  std::map<std::string, uint32_t> components;
  const base::FilePath probeable_components_path =
      component_type_config_path.Append(kCrosProbeableComponentsPath);
  for (int i = 0; i < kMaxSsfcProbeableComponentNum; ++i) {
    const base::FilePath component_path =
        probeable_components_path.Append(base::NumberToString(i));
    std::string identifier, value_str;
    uint32_t value;
    if (cros_config_->GetString(component_path.value(),
                                kCrosProbeableComponentsIdentifierKey,
                                &identifier) &&
        cros_config_->GetString(component_path.value(),
                                kCrosProbeableComponentsValueKey, &value_str) &&
        base::StringToUint(value_str, &value)) {
      components[identifier] = value;
    } else {
      break;
    }
  }
  return components;
}

}  // namespace rmad
