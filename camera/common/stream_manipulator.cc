/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common/stream_manipulator.h"

#if USE_CAMERA_FEATURE_HDRNET
#include "features/hdrnet/hdrnet_stream_manipulator.h"
#endif

#include "features/zsl/zsl_stream_manipulator.h"

namespace cros {

// static
std::vector<std::unique_ptr<StreamManipulator>>
StreamManipulator::GetEnabledStreamManipulators(Options options) {
  std::vector<std::unique_ptr<StreamManipulator>> stream_manipulators;

#if USE_CAMERA_FEATURE_HDRNET
  // TODO(jcliang): Update the camera module name here when the names are
  // updated in the HAL (b/194471449).
  constexpr const char kIntelIpu6CameraModuleName[] = "Intel Camera3HAL Module";
  if (options.camera_module_name == kIntelIpu6CameraModuleName) {
    stream_manipulators.emplace_back(
        std::make_unique<HdrNetStreamManipulator>());
  }
#endif

  if (options.enable_cros_zsl) {
    stream_manipulators.emplace_back(std::make_unique<ZslStreamManipulator>());
  }

  return stream_manipulators;
}

}  // namespace cros
