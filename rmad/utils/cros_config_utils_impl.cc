// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/utils/cros_config_utils_impl.h"

#include <algorithm>
#include <fstream>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <base/json/json_reader.h>
#include <base/logging.h>
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

constexpr char kCrosRootKey[] = "/";
constexpr char kCrosModelNameKey[] = "name";
constexpr char kCrosIdentityKey[] = "identity";
constexpr char kCrosIdentitySkuKey[] = "sku-id";
constexpr char kCrosIdentityCustomLabelTagKey[] = "custom-label-tag";

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

bool CrosConfigUtilsImpl::GetModelName(std::string* model_name) const {
  DCHECK(model_name);

  return cros_config_->GetString(kCrosRootKey, kCrosModelNameKey, model_name);
}

bool CrosConfigUtilsImpl::GetCurrentSkuId(int* sku_id) const {
  DCHECK(sku_id);

  std::string sku_id_str;
  if (!cros_config_->GetString(
          std::string(kCrosRootKey) + std::string(kCrosIdentityKey),
          kCrosIdentitySkuKey, &sku_id_str)) {
    return false;
  }

  return base::StringToInt(sku_id_str, sku_id);
}

bool CrosConfigUtilsImpl::GetCurrentCustomLabelTag(
    std::string* custom_label_tag) const {
  DCHECK(custom_label_tag);

  return cros_config_->GetString(
      std::string(kCrosRootKey) + std::string(kCrosIdentityKey),
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

  base::Value* cros_configs = cros.FindListKey(kChromeosConfigs);
  if (!cros_configs) {
    LOG(ERROR) << "Failed to get the configs section from the file";
    return false;
  }

  DCHECK(cros_configs->is_list());
  for (const auto& config : cros_configs->GetList()) {
    DCHECK(config.is_dict());
    const std::string* name = config.FindStringKey(kCrosModelNameKey);
    if (!name || *name != model_name) {
      continue;
    }

    const base::Value* identity = config.FindKey(kCrosIdentityKey);
    DCHECK(identity->is_dict());
    const base::Value* item = identity->FindKey(key);
    if (item) {
      items.insert(item->Clone());
    }
  }

  for (auto& item : items) {
    list->push_back(item.Clone());
  }
  return true;
}

}  // namespace rmad
