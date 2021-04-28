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

namespace cros {

// The Config class holds all the settings that controls the operation and
// behaviors of the HDRnet pipeline.
class HdrNetConfig {
 public:
  // The default HDRnet config file. The file should contain a JSON map for the
  // options defined below.
  static constexpr const char kHdrNetConfigFile[] = "/run/camera/hdrnet.config";

  struct Options {
    // Enables the HDRnet pipeline to produce output frames.
    bool enable = true;

    // The HDR ratio use for HDRnet rendering. Only effective if Gcam AE isn't
    // running.
    float hdr_ratio = 3.0;

    // Enables Gcam AE to produce exposure settings and HDR ratio.
    bool gcam_ae_enable = true;

    // Controls the frequency Gcam AE is run. The AE will run every
    // |gcam_ae_interval| frames.
    int gcam_ae_interval = 5;

    // Enables face ROI for Gcam AE.
    bool face_detection_enable = true;

    // The manual exposure compensation set to every capture request.
    int32_t exp_comp = 0;
  };

  explicit HdrNetConfig(const char* config_file_path = kHdrNetConfigFile);

  HdrNetConfig(const HdrNetConfig& other) = delete;
  HdrNetConfig& operator=(const HdrNetConfig& other) = delete;
  ~HdrNetConfig() = default;

  Options GetOptions();

 private:
  bool ReadConfigFile();
  void OnConfigFileUpdated(const base::FilePath& file_path, bool error);

  base::FilePath config_file_path_;
  base::FilePathWatcher file_path_watcher_;

  base::Lock options_lock_;
  Options options_ GUARDED_BY(options_lock_);
};

}  // namespace cros

#endif  // CAMERA_FEATURES_HDRNET_HDRNET_CONFIG_H_
