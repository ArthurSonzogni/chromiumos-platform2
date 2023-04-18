// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/utils/cros_config_utils_impl.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <base/json/json_reader.h>
#include <base/logging.h>
#include <base/strings/stringprintf.h>
#include <base/strings/string_number_conversions.h>
#include <chromeos-config/libcros_config/cros_config.h>

#include "rmad/utils/json_store.h"

namespace rmad {

namespace {

// TODO(genechang): We should build the configuration ourselves to
// prevent possible changes to the configuration file in the future.
const std::string kChromeosConfigPath(
    "/usr/share/chromeos-config/yaml/config.yaml");

constexpr char kChromeos[] = "chromeos";
constexpr char kChromeosConfigs[] = "configs";

// cros_config root path.
constexpr char kCrosRootPath[] = "/";
constexpr char kCrosModelNameKey[] = "name";

// cros_config identity path.
constexpr char kCrosIdentityPath[] = "identity";
constexpr char kCrosIdentitySkuKey[] = "sku-id";
constexpr char kCrosIdentityCustomLabelTagKey[] = "custom-label-tag";

// cros_config rmad path.
constexpr char kCrosRmadPath[] = "/rmad";
constexpr char kCrosRmadEnabledKey[] = "enabled";
constexpr char kCrosRmadHasCbiKey[] = "has-cbi";

// cros_config rmad/ssfc path.
constexpr char kCrosRmadSsfcPath[] = "/rmad/ssfc";
constexpr char kCrosRmadSsfcMaskKey[] = "mask";
constexpr char kCrosRmadSsfcComponentTypeConfigsPath[] =
    "/rmad/ssfc/component-type-configs";
constexpr char kCrosRmadSsfcComponentTypeKey[] = "component-type";
constexpr char kCrosRmadSsfcDefaultValueKey[] = "default-value";
constexpr char kCrosRmadSsfcProbeableComponentsRelPath[] =
    "probeable-components";
constexpr char kCrosRmadSsfcIdentifierKey[] = "identifier";
constexpr char kCrosRmadSsfcValueKey[] = "value";
constexpr int kMaxSsfcComponentTypeNum = 32;
constexpr int kMaxSsfcProbeableComponentNum = 1024;

constexpr char kTrueStr[] = "true";
constexpr char kUndefinedComponentType[] = "undefined_component_type";

}  // namespace

CrosConfigUtilsImpl::CrosConfigUtilsImpl()
    : config_file_path_(kChromeosConfigPath) {
  cros_config_ = std::make_unique<brillo::CrosConfig>();
}

CrosConfigUtilsImpl::CrosConfigUtilsImpl(
    const std::string& config_file_path,
    std::unique_ptr<brillo::CrosConfigInterface> cros_config)
    : config_file_path_(config_file_path),
      cros_config_(std::move(cros_config)) {}

bool CrosConfigUtilsImpl::GetRmadConfig(RmadConfig* config) const {
  DCHECK(config);

  config->enabled =
      GetBooleanWithDefault(kCrosRmadPath, kCrosRmadEnabledKey, false);
  config->has_cbi =
      GetBooleanWithDefault(kCrosRmadPath, kCrosRmadHasCbiKey, false);
  config->ssfc = GetSsfc();

  return true;
}

bool CrosConfigUtilsImpl::GetModelName(std::string* model_name) const {
  DCHECK(model_name);

  return cros_config_->GetString(kCrosRootPath, kCrosModelNameKey, model_name);
}

bool CrosConfigUtilsImpl::GetSkuId(int* sku_id) const {
  DCHECK(sku_id);

  std::string sku_id_str;
  if (!cros_config_->GetString(
          std::string(kCrosRootPath) + std::string(kCrosIdentityPath),
          kCrosIdentitySkuKey, &sku_id_str)) {
    return false;
  }

  return base::StringToInt(sku_id_str, sku_id);
}

bool CrosConfigUtilsImpl::GetCustomLabelTag(
    std::string* custom_label_tag) const {
  DCHECK(custom_label_tag);

  return cros_config_->GetString(
      std::string(kCrosRootPath) + std::string(kCrosIdentityPath),
      kCrosIdentityCustomLabelTagKey, custom_label_tag);
}

bool CrosConfigUtilsImpl::GetSkuIdList(std::vector<int>* sku_id_list) const {
  DCHECK(sku_id_list);

  std::vector<base::Value> values;
  if (!GetMatchedItemsFromIdentity(kCrosIdentitySkuKey, &values)) {
    return false;
  }

  sku_id_list->clear();
  for (auto& value : values) {
    if (value.is_int()) {
      sku_id_list->push_back(value.GetInt());
    }
  }

  sort(sku_id_list->begin(), sku_id_list->end());
  return true;
}

bool CrosConfigUtilsImpl::GetCustomLabelTagList(
    std::vector<std::string>* custom_label_tag_list) const {
  DCHECK(custom_label_tag_list);

  std::vector<base::Value> values;
  if (!GetMatchedItemsFromIdentity(kCrosIdentityCustomLabelTagKey, &values)) {
    return false;
  }

  custom_label_tag_list->clear();
  for (auto& value : values) {
    if (value.is_string()) {
      custom_label_tag_list->push_back(value.GetString());
    }
  }

  sort(custom_label_tag_list->begin(), custom_label_tag_list->end());
  return true;
}

bool CrosConfigUtilsImpl::GetMatchedItemsFromIdentity(
    const std::string& key, std::vector<base::Value>* list) const {
  DCHECK(list);

  list->clear();
  std::set<base::Value> items;

  std::string model_name;
  if (!GetModelName(&model_name)) {
    LOG(ERROR) << "Failed to get model name for comparison";
    return false;
  }

  scoped_refptr<JsonStore> json_store =
      base::MakeRefCounted<JsonStore>(base::FilePath(config_file_path_));

  if (auto error = json_store->GetReadError();
      error != JsonStore::READ_ERROR_NONE) {
    LOG_STREAM(ERROR) << "Failed to parse file due to error code #" << error;
    return false;
  }

  base::Value cros;
  if (!json_store->GetValue(kChromeos, &cros)) {
    LOG(ERROR) << "Failed to get the chromeos section from the file";
    return false;
  }
  DCHECK(cros.is_dict());

  base::Value::List* cros_configs = cros.GetDict().FindList(kChromeosConfigs);
  if (!cros_configs) {
    LOG(ERROR) << "Failed to get the configs section from the file";
    return false;
  }

  for (const auto& config : *cros_configs) {
    DCHECK(config.is_dict());
    const std::string* name = config.GetDict().FindString(kCrosModelNameKey);
    if (!name || *name != model_name) {
      continue;
    }

    const base::Value::Dict* identity =
        config.GetDict().FindDict(kCrosIdentityPath);
    const base::Value* item = identity->Find(key);
    if (item) {
      items.insert(item->Clone());
    }
  }

  for (auto& item : items) {
    list->push_back(item.Clone());
  }
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

SsfcConfig CrosConfigUtilsImpl::GetSsfc() const {
  SsfcConfig ssfc;
  ssfc.mask = GetUintWithDefault(kCrosRmadSsfcPath, kCrosRmadSsfcMaskKey, 0);
  ssfc.component_type_configs = GetSsfcComponentTypeConfigs();
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
CrosConfigUtilsImpl::GetSsfcComponentTypeConfigs() const {
  std::vector<SsfcComponentTypeConfig> component_type_configs;
  for (int i = 0; i < kMaxSsfcComponentTypeNum; ++i) {
    const std::string path =
        base::StringPrintf("%s/%d", kCrosRmadSsfcComponentTypeConfigsPath, i);
    SsfcComponentTypeConfig component_type_config =
        GetSsfcComponentTypeConfig(path);
    if (component_type_config.probeable_components.size()) {
      component_type_configs.emplace_back(std::move(component_type_config));
    } else {
      break;
    }
  }
  return component_type_configs;
}

SsfcComponentTypeConfig CrosConfigUtilsImpl::GetSsfcComponentTypeConfig(
    const std::string& path) const {
  SsfcComponentTypeConfig config;
  config.component_type = GetStringWithDefault(
      path, kCrosRmadSsfcComponentTypeKey, kUndefinedComponentType);
  config.default_value =
      GetUintWithDefault(path, kCrosRmadSsfcDefaultValueKey, 0);
  config.probeable_components = GetSsfcProbeableComponents(path);
  return config;
}

std::map<std::string, uint32_t> CrosConfigUtilsImpl::GetSsfcProbeableComponents(
    const std::string& path) const {
  std::map<std::string, uint32_t> components;
  for (int i = 0; i < kMaxSsfcProbeableComponentNum; ++i) {
    const std::string component_path = base::StringPrintf(
        "%s/%s/%d", path.c_str(), kCrosRmadSsfcProbeableComponentsRelPath, i);
    std::string identifier, value_str;
    uint32_t value;
    if (cros_config_->GetString(component_path, kCrosRmadSsfcIdentifierKey,
                                &identifier) &&
        cros_config_->GetString(component_path, kCrosRmadSsfcValueKey,
                                &value_str) &&
        base::StringToUint(value_str, &value)) {
      components[identifier] = value;
    } else {
      break;
    }
  }
  return components;
}

}  // namespace rmad
