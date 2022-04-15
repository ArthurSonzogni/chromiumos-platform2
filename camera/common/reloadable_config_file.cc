/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common/reloadable_config_file.h"

#include <iomanip>
#include <optional>
#include <string>
#include <utility>

#include <base/files/file_util.h>
#include <base/json/json_reader.h>
#include <base/json/json_writer.h>

#include "cros-camera/common.h"

namespace cros {

ReloadableConfigFile::ReloadableConfigFile(
    base::FilePath default_config_file_path,
    base::FilePath override_config_file_path)
    : default_config_file_path_(default_config_file_path) {
  base::AutoLock lock(options_lock_);
  ReadConfigFileLocked(default_config_file_path_);
  if (!override_config_file_path.empty()) {
    override_config_file_path_ = base::FilePath(override_config_file_path);
    // Override config file is optional and may not exist. Check before read to
    // avoid printing the error message in ReadConfigFileLocked().
    if (base::PathExists(override_config_file_path_)) {
      ReadConfigFileLocked(override_config_file_path_);
    }
    bool ret = override_file_path_watcher_.Watch(
        override_config_file_path_, base::FilePathWatcher::Type::kNonRecursive,
        base::BindRepeating(&ReloadableConfigFile::OnConfigFileUpdated,
                            base::Unretained(this)));
    DCHECK(ret) << "Can't monitor override config file path: "
                << override_config_file_path_;
  }
}

void ReloadableConfigFile::SetCallback(OptionsUpdateCallback callback) {
  options_update_callback_ = std::move(callback);
  base::AutoLock lock(options_lock_);
  if (!json_values_.is_none()) {
    options_update_callback_.Run(json_values_);
  }
}

void ReloadableConfigFile::UpdateOption(std::string key, base::Value value) {
  base::AutoLock lock(options_lock_);
  json_values_.SetKey(key, std::move(value));
  WriteConfigFileLocked(override_config_file_path_);
}

void ReloadableConfigFile::ReadConfigFileLocked(
    const base::FilePath& file_path) {
  options_lock_.AssertAcquired();
  if (!base::PathExists(file_path)) {
    LOGF(ERROR) << "Config file does not exist: " << file_path;
    return;
  }
  // Limiting config file size to 64KB. Increase this if needed.
  constexpr size_t kConfigFileMaxSize = 65536;
  std::string contents;
  CHECK(base::ReadFileToStringWithMaxSize(file_path, &contents,
                                          kConfigFileMaxSize));
  std::optional<base::Value> json_values =
      base::JSONReader::Read(contents, base::JSON_ALLOW_TRAILING_COMMAS);
  if (!json_values) {
    LOGF(ERROR) << "Failed to load the config file content of " << file_path;
    return;
  }
  if (json_values_.is_dict() && json_values->is_dict()) {
    // Merge the new and existing config if both are dictionary. Keys that are
    // present both in the existing and new config will be overwritten with the
    // new value.
    json_values_.MergeDictionary(&json_values.value());
  } else {
    json_values_ = std::move(*json_values);
  }
}

void ReloadableConfigFile::WriteConfigFileLocked(
    const base::FilePath& file_path) {
  options_lock_.AssertAcquired();
  std::string json_string;
  if (!base::JSONWriter::WriteWithOptions(
          json_values_, base::JSONWriter::OPTIONS_PRETTY_PRINT, &json_string)) {
    LOGF(WARNING) << "Can't jsonify config settings";
    return;
  }
  if (!base::WriteFile(file_path, json_string)) {
    LOGF(WARNING) << "Can't write config settings to "
                  << std::quoted(file_path.value());
  }
}

void ReloadableConfigFile::OnConfigFileUpdated(const base::FilePath& file_path,
                                               bool error) {
  base::AutoLock lock(options_lock_);
  ReadConfigFileLocked(override_config_file_path_);
  if (options_update_callback_) {
    options_update_callback_.Run(json_values_.Clone());
  }
}

bool LoadIfExist(const base::Value& json_values,
                 const char* key,
                 float* output) {
  if (!output) {
    LOGF(ERROR) << "output cannot be nullptr";
    return false;
  }
  auto value = json_values.FindDoubleKey(key);
  if (!value) {
    return false;
  }
  *output = *value;
  return true;
}

bool LoadIfExist(const base::Value& json_values, const char* key, int* output) {
  if (!output) {
    LOGF(ERROR) << "output cannot be nullptr";
    return false;
  }
  auto value = json_values.FindIntKey(key);
  if (!value) {
    return false;
  }
  *output = *value;
  return true;
}

bool LoadIfExist(const base::Value& json_values,
                 const char* key,
                 bool* output) {
  if (!output) {
    LOGF(ERROR) << "output cannot be nullptr";
    return false;
  }
  auto value = json_values.FindBoolKey(key);
  if (!value) {
    return false;
  }
  *output = *value;
  return true;
}

}  // namespace cros
