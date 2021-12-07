/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_COMMON_RELOADABLE_CONFIG_FILE_H_
#define CAMERA_COMMON_RELOADABLE_CONFIG_FILE_H_

#include <base/callback_helpers.h>
#include <base/files/file_path.h>
#include <base/files/file_path_watcher.h>
#include <base/synchronization/lock.h>
#include <base/values.h>

namespace cros {

// An abstraction for a JSON-based config file. ReloadableConfigFile by default
// loads the config from a given default path, which usually resides in the root
// filesystem and is read-only. ReloadableConfigFile can be further configured
// to monitor an override config file and it will reload new configs from the
// override config file when the file content changes.
class ReloadableConfigFile {
 public:
  using OptionsUpdateCallback =
      base::RepeatingCallback<void(const base::Value&)>;

  // The config is read from |default_config_file_path| first if the path
  // exists, otherwise we use the default values set above.
  // |override_config_file_path| will be actively monitored at run-time, and we
  // will overwrite the existing |options_| values with the ones present in the
  // override config file. The config in the override file doesn't have to
  // include all the options and it can update only a subset of the options.
  ReloadableConfigFile(
      base::FilePath default_config_file_path,
      base::FilePath override_config_file_path = base::FilePath());
  ReloadableConfigFile(const ReloadableConfigFile& other) = delete;
  ReloadableConfigFile& operator=(const ReloadableConfigFile& other) = delete;
  ~ReloadableConfigFile() = default;

  void SetCallback(OptionsUpdateCallback callback);

 private:
  void ReadConfigFileLocked(const base::FilePath& file_path);
  void OnConfigFileUpdated(const base::FilePath& file_path, bool error);

  OptionsUpdateCallback options_update_callback_ = base::NullCallback();

  // The default config file path. Usually this points to the device-specific
  // tuning file shipped with the OS image.
  base::FilePath default_config_file_path_;
  // The override config file path. The override config is used to override the
  // default config at run-time for development or debugging purposes.
  base::FilePath override_config_file_path_;
  base::FilePathWatcher override_file_path_watcher_;

  base::Lock options_lock_;
  base::Value json_values_ GUARDED_BY(options_lock_);
};

// Helper functions to look up |key| in |json_values| and, if key exists, load
// the corresponding value into |output|. Returns true if |output| is loaded
// with the value found, false otherwise.
bool LoadIfExist(const base::Value& json_values,
                 const char* key,
                 float* output);
bool LoadIfExist(const base::Value& json_values, const char* key, int* output);
bool LoadIfExist(const base::Value& json_values, const char* key, bool* output);

}  // namespace cros

#endif  // CAMERA_COMMON_RELOADABLE_CONFIG_FILE_H_
