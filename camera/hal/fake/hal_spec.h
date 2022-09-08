/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_HAL_FAKE_HAL_SPEC_H_
#define CAMERA_HAL_FAKE_HAL_SPEC_H_

#include <optional>
#include <vector>

#include <base/values.h>

namespace cros {

struct CameraSpec {
  int id = 0;
  bool connected = false;
};

struct HalSpec {
  std::vector<CameraSpec> cameras;
};

std::optional<HalSpec> ParseHalSpecFromJsonValue(const base::Value& value);

}  // namespace cros

#endif  // CAMERA_HAL_FAKE_HAL_SPEC_H_
