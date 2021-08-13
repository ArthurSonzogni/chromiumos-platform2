/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common/stream_manipulator.h"

#if USE_CAMERA_FEATURE_HDRNET
#include <base/files/file_util.h>

#include "cros-camera/constants.h"
#include "features/hdrnet/hdrnet_stream_manipulator.h"
#endif

#include "features/zsl/zsl_stream_manipulator.h"

namespace cros {

void MaybeEnableHdrNetStreamManipulator(
    const StreamManipulator::Options& options,
    std::vector<std::unique_ptr<StreamManipulator>>* out_stream_manipulators) {
#if USE_CAMERA_FEATURE_HDRNET
  if (base::PathExists(base::FilePath(constants::kForceDisableHdrNetPath))) {
    // HDRnet is forcibly disabled.
    return;
  }

  if (base::PathExists(base::FilePath(constants::kForceEnableHdrNetPath)) ||
      options.enable_hdrnet) {
    // HDRnet is enabled forcibly or by the device setting.

    // TODO(jcliang): Update the camera module name here when the names are
    // updated in the HAL (b/194471449).
    constexpr const char kIntelIpu6CameraModuleName[] =
        "Intel Camera3HAL Module";
    if (options.camera_module_name == kIntelIpu6CameraModuleName) {
      out_stream_manipulators->emplace_back(
          std::make_unique<HdrNetStreamManipulator>());
    }
  }
#endif
}

// static
std::vector<std::unique_ptr<StreamManipulator>>
StreamManipulator::GetEnabledStreamManipulators(Options options) {
  std::vector<std::unique_ptr<StreamManipulator>> stream_manipulators;

  MaybeEnableHdrNetStreamManipulator(options, &stream_manipulators);

  if (options.enable_cros_zsl) {
    stream_manipulators.emplace_back(std::make_unique<ZslStreamManipulator>());
  }

  return stream_manipulators;
}

}  // namespace cros
