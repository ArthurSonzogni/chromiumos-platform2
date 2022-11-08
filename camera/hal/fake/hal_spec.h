/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_HAL_FAKE_HAL_SPEC_H_
#define CAMERA_HAL_FAKE_HAL_SPEC_H_

#include <optional>
#include <variant>
#include <vector>

#include <base/files/file_path.h>
#include <base/values.h>

namespace cros {

struct FramesFileSpec {
  base::FilePath path;

  bool operator==(const FramesFileSpec& rhs) const { return path == rhs.path; }
  bool operator!=(const FramesFileSpec& rhs) const { return !(*this == rhs); }
};

struct FramesTestPatternSpec {
  bool operator==(const FramesTestPatternSpec& rhs) const { return true; }
  bool operator!=(const FramesTestPatternSpec& rhs) const {
    return !(*this == rhs);
  }
};

using FramesSpec = std::variant<FramesFileSpec, FramesTestPatternSpec>;

struct CameraSpec {
  int id = 0;
  bool connected = false;
  FramesSpec frames = FramesTestPatternSpec();
};

struct HalSpec {
  std::vector<CameraSpec> cameras;
};

std::optional<HalSpec> ParseHalSpecFromJsonValue(const base::Value& value);

}  // namespace cros

#endif  // CAMERA_HAL_FAKE_HAL_SPEC_H_
