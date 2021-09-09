/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_FEATURES_HDRNET_HDRNET_CONFIG_H_
#define CAMERA_FEATURES_HDRNET_HDRNET_CONFIG_H_

#include <base/files/file_path.h>
#include <base/files/file_path_watcher.h>
#include <base/synchronization/lock.h>

#include "features/hdrnet/ae_info.h"

namespace cros {

// The Config class holds all the settings that controls the operation and
// behaviors of the HDRnet pipeline.
class HdrNetConfig {
 public:
  // The default HDRnet config file. The file should contain a JSON map for the
  // options defined below.
  static constexpr const char kDefaultHdrNetConfigFile[] =
      "/etc/camera/hdrnet.config";
  static constexpr const char kOverrideHdrNetConfigFile[] =
      "/run/camera/hdrnet.config";

  struct Options {
    // Enables the HDRnet pipeline to produce output frames.
    bool hdrnet_enable = true;

    // Enables Gcam AE to produce exposure settings and HDR ratio.
    bool gcam_ae_enable = true;

    // Controls the duty cycle of Gcam AE. The AE will run every
    // |ae_frame_interval| frames.
    int ae_frame_interval = 2;

    // Controls the max HDR ratio passed to Gcam AE.
    float max_hdr_ratio = 10.0f;

    // Controls how Gcam AE gets the AE stats input parameters.
    AeStatsInputMode ae_stats_input_mode = AeStatsInputMode::kFromVendorAeStats;

    // Controls how HdrNetAeController overrides camera HAL's AE decision.
    AeOverrideMode ae_override_mode = AeOverrideMode::kWithExposureCompensation;

    // Uses CrOS face detector for face detection instead of the vendor one.
    bool use_cros_face_detector = false;

    // Controls the duty cycle of CrOS face detector. The face detector will run
    // every |fd_frame_interval| frames.
    int fd_frame_interval = 10;

    // The HDR ratio use for HDRnet rendering. Only effective if Gcam AE isn't
    // running.
    float hdr_ratio = 3.0;

    // The exposure compensation in stops set to every capture request.
    float exposure_compensation = 0.0f;

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
