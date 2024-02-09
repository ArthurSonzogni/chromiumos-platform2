/*
 * Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_INCLUDE_CROS_CAMERA_ANGLE_STATE_H_
#define CAMERA_INCLUDE_CROS_CAMERA_ANGLE_STATE_H_

#include <stdlib.h>
#include <filesystem>

namespace cros {

constexpr char kAngleDisabledFilePath[] = "/run/camera/angle_disabled";

inline bool AngleEnabled() {
  return !std::filesystem::exists(kAngleDisabledFilePath);
}

inline void EnableAngle() {
  std::filesystem::remove(kAngleDisabledFilePath);
}

inline void DisableAngle() {
  FILE* angle_disabled_file = fopen(kAngleDisabledFilePath, "w");
  fclose(angle_disabled_file);
}

}  // namespace cros

#endif  // CAMERA_INCLUDE_CROS_CAMERA_ANGLE_STATE_H_
