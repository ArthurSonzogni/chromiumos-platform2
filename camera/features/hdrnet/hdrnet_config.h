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
  // By default the config is loaded from the feature config file path specified
  // in the feature profile. For testing or debugging, the feature config can be
  // override by the config override file below. The file should contain a JSON
  // map for the options defined below.
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
};

}  // namespace cros

#endif  // CAMERA_FEATURES_HDRNET_HDRNET_CONFIG_H_
