/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common/reloadable_config_file.h"

#include <string>
#include <utility>

#include <base/files/file_util.h>
#include <base/json/json_reader.h>

#include "cros-camera/common.h"

namespace cros {

ReloadableConfigFile::ReloadableConfigFile(
    const char* default_config_file_path, const char* override_config_file_path)
    : default_config_file_path_(default_config_file_path) {
  base::AutoLock lock(options_lock_);
  ReadConfigFileLocked(default_config_file_path_);
  if (override_config_file_path) {
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
  options_update_callback_.Run(json_values_);
}

void ReloadableConfigFile::ReadConfigFileLocked(
    const base::FilePath& file_path) {
  if (!base::PathExists(file_path)) {
    LOGF(ERROR) << "Config file does not exist: " << file_path;
    return;
  }
  constexpr size_t kConfigFileMaxSize = 1024;
  std::string contents;
  CHECK(base::ReadFileToStringWithMaxSize(file_path, &contents,
                                          kConfigFileMaxSize));
  base::Optional<base::Value> json_values =
      base::JSONReader::Read(contents, base::JSON_ALLOW_TRAILING_COMMAS);
  if (!json_values) {
    LOGF(ERROR) << "Failed to load the config file content of " << file_path;
    return;
  }
  json_values_ = std::move(*json_values);
}

void ReloadableConfigFile::OnConfigFileUpdated(const base::FilePath& file_path,
                                               bool error) {
  base::AutoLock lock(options_lock_);
  ReadConfigFileLocked(override_config_file_path_);
  if (options_update_callback_) {
    options_update_callback_.Run(json_values_.Clone());
  }
}

}  // namespace cros
