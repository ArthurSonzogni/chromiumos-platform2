// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime_probe/probe_config.h"

#include <memory>
#include <optional>
#include <utility>

#include <base/barrier_callback.h>
#include <base/files/file_util.h>
#include <base/functional/bind.h>
#include <base/functional/callback.h>
#include <base/json/json_reader.h>
#include <base/hash/sha1.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/values.h>
#include <brillo/map_utils.h>

#include "runtime_probe/component_category.h"

namespace runtime_probe {

namespace {

std::string HashProbeConfigSHA1(const std::string& content) {
  const auto& hash_val = base::SHA1HashString(content);
  return base::HexEncode(hash_val.data(), hash_val.size());
}

// Callback to handle a single result from |ComponentCategory::EvalAsync|.
void OnComponentCategoryEvalCompleted(
    base::OnceCallback<void(std::pair<std::string, base::Value::List>)>
        callback,
    const std::string& category_name,
    base::Value::List probe_result) {
  std::move(callback).Run(
      std::make_pair(category_name, std::move(probe_result)));
}

void CollectComponentCategoryResults(
    base::OnceCallback<void(base::Value::Dict)> callback,
    std::vector<std::pair<std::string, base::Value::List>> probe_results) {
  base::Value::Dict results;
  for (auto& [category_name, probe_result] : probe_results) {
    results.Set(category_name, std::move(probe_result));
  }
  std::move(callback).Run(std::move(results));
}

}  // namespace

std::optional<ProbeConfig> ProbeConfig::FromFile(
    const base::FilePath& file_path) {
  DVLOG(3) << "ProbeConfig::FromFile: " << file_path;
  std::string config_json;
  if (!base::ReadFileToString(file_path, &config_json)) {
    return std::nullopt;
  }
  auto json_val = base::JSONReader::Read(config_json, base::JSON_PARSE_RFC);
  if (!json_val || !json_val->is_dict()) {
    DVLOG(3) << "Failed to parse probe config as JSON.";
    return std::nullopt;
  }

  auto absolute_path = base::MakeAbsoluteFilePath(file_path);
  auto probe_config_sha1_hash = HashProbeConfigSHA1(config_json);
  DVLOG(3) << "SHA1 hash of probe config: " << probe_config_sha1_hash;

  auto config = ProbeConfig::FromValue(*json_val);
  if (!config)
    return std::nullopt;
  config->path_ = std::move(absolute_path);
  config->checksum_ = std::move(probe_config_sha1_hash);
  return config;
}

std::optional<ProbeConfig> ProbeConfig::FromValue(const base::Value& dv) {
  if (!dv.is_dict()) {
    LOG(ERROR) << "ProbeConfig::FromValue takes a dictionary as parameter";
    return std::nullopt;
  }

  ProbeConfig instance;

  for (const auto& entry : dv.GetDict()) {
    const auto& category_name = entry.first;
    const auto& value = entry.second;
    auto category = ComponentCategory::FromValue(category_name, value);
    if (!category) {
      LOG(ERROR) << "Category " << category_name
                 << " doesn't contain a valid probe statement.";
      return std::nullopt;
    }
    instance.category_[category_name] = std::move(category);
  }

  return instance;
}

void ProbeConfig::Eval(
    base::OnceCallback<void(base::Value::Dict)> callback) const {
  Eval(brillo::GetMapKeysAsVector(category_), std::move(callback));
}

void ProbeConfig::Eval(
    const std::vector<std::string>& category,
    base::OnceCallback<void(base::Value::Dict)> callback) const {
  std::vector<std::string> valid_category;
  for (const auto& c : category) {
    auto it = category_.find(c);
    if (it == category_.end()) {
      LOG(ERROR) << "Category " << c << " is not defined";
      continue;
    }
    valid_category.push_back(it->first);
  }

  const auto barrier_callback =
      base::BarrierCallback<std::pair<std::string, base::Value::List>>(
          valid_category.size(),
          base::BindOnce(&CollectComponentCategoryResults,
                         std::move(callback)));

  for (const auto& c : valid_category)
    category_.at(c)->Eval(
        base::BindOnce(&OnComponentCategoryEvalCompleted, barrier_callback, c));
}

ComponentCategory* ProbeConfig::GetComponentCategory(
    const std::string& category_name) const {
  auto itr = category_.find(category_name);
  if (itr == category_.end())
    return nullptr;
  return itr->second.get();
}

}  // namespace runtime_probe
