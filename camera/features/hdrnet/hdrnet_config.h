/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_FEATURES_HDRNET_HDRNET_CONFIG_H_
#define CAMERA_FEATURES_HDRNET_HDRNET_CONFIG_H_

#include <base/containers/flat_map.h>
#include <base/files/file_path.h>
#include <base/files/file_path_watcher.h>
#include <base/synchronization/lock.h>

namespace cros {

// The Config class holds all the settings that controls the operation and
// behaviors of the HDRnet pipeline.
class HdrNetConfig {
 public:
  // The default HDRnet config file. The file should contain a JSON map for the
  // options defined below.
  static constexpr const char kDefaultHdrNetConfigFile[] =
      "/etc/camera/hdrnet_config.json";
  static constexpr const char kOverrideHdrNetConfigFile[] =
      "/run/camera/hdrnet_config.json";

  struct Options {
    // Enables the HDRnet pipeline to produce output frames.
    bool hdrnet_enable = true;

    // The HDR ratio use for HDRnet rendering. Only effective if Gcam AE isn't
    // running.
    float hdr_ratio = 3.0;

    // Dumps intermediate processing buffers for debugging.
    bool dump_buffer = false;

    // Whether to log per-frame metadata using MetadataLogger.
    bool log_frame_metadata = false;
  };

  // The config is read from |default_config_file_path| first if the path
  // exists, otherwise we use the default values set above.
  // |override_config_file_path| will be actively monitored at run-time, and we
  // will overwrite the existing |options_| values with the ones present in the
  // override config file. The config in the override file doesn't have to
  // include all the options and it can update only a subset of the options.
  HdrNetConfig(
      const char* default_config_file_path = kDefaultHdrNetConfigFile,
      const char* override_config_file_path = kOverrideHdrNetConfigFile);

  HdrNetConfig(const HdrNetConfig& other) = delete;
  HdrNetConfig& operator=(const HdrNetConfig& other) = delete;
  ~HdrNetConfig() = default;

  Options GetOptions();

 private:
  bool ReadConfigFile(const base::FilePath& file_path);
  void OnConfigFileUpdated(const base::FilePath& file_path, bool error);

  // The default config file path. Usually this points to the device-specific
  // tuning file shipped with the OS image.
  base::FilePath default_config_file_path_;
  // The override config file path. The override config is used to override the
  // default config at run-time for development or debugging purposes.
  base::FilePath override_config_file_path_;
  base::FilePathWatcher override_file_path_watcher_;

  base::Lock options_lock_;
  Options options_ GUARDED_BY(options_lock_);
};

}  // namespace cros

#endif  // CAMERA_FEATURES_HDRNET_HDRNET_CONFIG_H_
