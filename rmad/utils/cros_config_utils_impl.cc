// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/utils/cros_config_utils_impl.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <map>
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

#include "rmad/utils/json_store.h"

namespace rmad {

namespace {

constexpr char kChromeosConfigsRootPath[] =
    "/run/chromeos-config/private/v1/chromeos/configs";

// cros_config root path.
constexpr char kCrosRootPath[] = "/";
// cros_config property /name.
constexpr char kCrosModelNameKey[] = "name";
// cros_config property /brand-code.
constexpr char kCrosBrandCodeKey[] = "brand-code";

// cros_config path /identity.
constexpr char kCrosIdentityPath[] = "identity";
// cros_config property /identity/sku-id.
constexpr char kCrosIdentitySkuKey[] = "sku-id";
// cros_config property /identity/custom-label-tag.
constexpr char kCrosIdentityCustomLabelTagKey[] = "custom-label-tag";

// cros_config path /rmad.
constexpr char kCrosRmadPath[] = "rmad";
// cros_config property /rmad/enabled.
constexpr char kCrosRmadEnabledKey[] = "enabled";
// cros_config property /rmad/has-cbi.
constexpr char kCrosRmadHasCbiKey[] = "has-cbi";
// cros_config property /rmad/use-legacy-custom-label.
constexpr char kCrosRmadUseLegacyCustomLabelKey[] = "use-legacy-custom-label";
// cros_config path /rmad/ssfc.
constexpr char kCrosSsfcPath[] = "ssfc";
// cros_config property /ssfc/mask.
constexpr char kCrosSsfcMaskKey[] = "mask";
// cros_config path /rmad/ssfc/component-type-configs.
constexpr char kCrosComponentTypeConfigsPath[] = "component-type-configs";
// cros_config property /rmad/ssfc/component-type-configs/*/component-type.
constexpr char kCrosComponentTypeConfigsComponentTypeKey[] = "component-type";
// cros_config property /rmad/ssfc/component-type-configs/*/default-value.
constexpr char kCrosComponentTypeConfigsDefaultValueKey[] = "default-value";
// cros_config path /rmad/ssfc/component-type-configs/*/probeable-components.
constexpr char kCrosProbeableComponentsPath[] = "probeable-components";
// cros_config property
// /rmad/ssfc/component-type-configs/*/probeable-components/*/identifier.
constexpr char kCrosProbeableComponentsIdentifierKey[] = "identifier";
// cros_config property
// /rmad/ssfc/component-type-configs/*/probeable-components/*/value.
constexpr char kCrosProbeableComponentsValueKey[] = "value";

constexpr int kMaxSsfcComponentTypeNum = 32;
constexpr int kMaxSsfcProbeableComponentNum = 1024;

constexpr char kTrueStr[] = "true";
constexpr char kUndefinedComponentType[] = "undefined_component_type";

}  // namespace

CrosConfigUtilsImpl::CrosConfigUtilsImpl()
    : configs_root_path_(kChromeosConfigsRootPath) {
  cros_config_ = std::make_unique<brillo::CrosConfig>();
}

CrosConfigUtilsImpl::CrosConfigUtilsImpl(
    const std::string& configs_root_path,
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

bool CrosConfigUtilsImpl::GetSkuId(uint64_t* sku_id) const {
  DCHECK(sku_id);

  std::string sku_id_str;
  const base::FilePath identity_path =
      base::FilePath(kCrosRootPath).Append(kCrosIdentityPath);
  if (!cros_config_->GetString(identity_path.value(), kCrosIdentitySkuKey,
                               &sku_id_str)) {
    return false;
  }

  return base::StringToUint64(sku_id_str, sku_id);
}

bool CrosConfigUtilsImpl::GetCustomLabelTag(
    std::string* custom_label_tag) const {
  DCHECK(custom_label_tag);

  const base::FilePath identity_path =
      base::FilePath(kCrosRootPath).Append(kCrosIdentityPath);
  return cros_config_->GetString(
      identity_path.value(), kCrosIdentityCustomLabelTagKey, custom_label_tag);
}

bool CrosConfigUtilsImpl::GetSkuIdList(
    std::vector<uint64_t>* sku_id_list) const {
  DCHECK(sku_id_list);

  std::vector<std::string> values;
  if (!GetMatchedItemsFromCategory(kCrosIdentityPath, kCrosIdentitySkuKey,
                                   &values)) {
    return false;
  }

  sku_id_list->clear();
  for (auto& value : values) {
    uint64_t sku_id;
    if (!base::StringToUint64(value, &sku_id)) {
      LOG(ERROR) << "Failed to convert " << value << " to uint64_t";
      return false;
    }

    sku_id_list->push_back(sku_id);
  }

  sort(sku_id_list->begin(), sku_id_list->end());
  return true;
}

bool CrosConfigUtilsImpl::GetCustomLabelTagList(
    std::vector<std::string>* custom_label_tag_list) const {
  DCHECK(custom_label_tag_list);

  std::vector<std::string> values;
  if (!GetMatchedItemsFromCategory(kCrosIdentityPath,
                                   kCrosIdentityCustomLabelTagKey, &values,
                                   /*allow_empty=*/true)) {
    return false;
  }

  custom_label_tag_list->clear();
  for (auto& value : values) {
    custom_label_tag_list->push_back(value);
  }

  sort(custom_label_tag_list->begin(), custom_label_tag_list->end());
  return true;
}

bool CrosConfigUtilsImpl::GetMatchedItemsFromCategory(
    const std::string& category,
    const std::string& key,
    std::vector<std::string>* list,
    bool allow_empty) const {
  DCHECK(list);

  std::string model_name;
  if (!GetModelName(&model_name)) {
    LOG(ERROR) << "Failed to get model name for comparison";
    return false;
  }

  std::set<std::string> items;
  base::FileEnumerator directories(base::FilePath(configs_root_path_), false,
                                   base::FileEnumerator::FileType::DIRECTORIES);
  for (base::FilePath path = directories.Next(); !path.empty();
       path = directories.Next()) {
    base::FilePath model_name_path = path.Append(kCrosModelNameKey);
    std::string model_name_str;
    if (!base::ReadFileToString(model_name_path, &model_name_str)) {
      LOG(WARNING) << "Failed to read model name from "
                   << model_name_path.value();
    }
    if (model_name != model_name_str) {
      continue;
    }

    base::FilePath key_path = path.Append(category).Append(key);
    std::string key_str = "";
    if (!base::ReadFileToString(key_path, &key_str)) {
      // This might be expected behavior. cros_config sometimes doesn't populate
      // attributes with empty strings.
      DLOG(WARNING) << "Failed to read key from " << key_path.value();
    }

    if (!key_str.empty() || allow_empty) {
      items.insert(key_str);
    }
  }

  *list = std::vector<std::string>(items.begin(), items.end());
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
