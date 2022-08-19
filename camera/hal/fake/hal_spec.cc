/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <string>
#include <vector>

#include <base/strings/string_util.h>
#include <base/strings/string_number_conversions.h>

#include "hal/fake/hal_spec.h"
#include "hal/fake/value_util.h"

namespace cros {

namespace {

constexpr char kCamerasKey[] = "cameras";
constexpr char kIdKey[] = "id";
constexpr char kConnectedKey[] = "connected";

std::vector<CameraSpec> ParseCameraSpecs(const ListWithPath& cameras_value) {
  std::vector<CameraSpec> camera_specs;
  for (auto c : cameras_value) {
    auto spec_value = GetIfDict(c);
    if (!spec_value) {
      continue;
    }

    CameraSpec camera_spec;

    if (auto id = GetValue<int>(*spec_value, kIdKey)) {
      camera_spec.id = *id;
    } else {
      // TODO(pihsun): Use generated ID for this case?
      continue;
    }
    camera_spec.connected = GetValue(*spec_value, kConnectedKey, false);

    camera_specs.push_back(camera_spec);
  }
  return camera_specs;
}

}  // namespace

std::optional<HalSpec> ParseHalSpecFromJsonValue(const base::Value& value) {
  HalSpec spec;

  ValueWithPath root = {&value, {}};

  auto root_dict = GetIfDict(root);
  if (!root_dict) {
    return {};
  }

  if (auto cameras = GetValue<ListWithPath>(*root_dict, kCamerasKey)) {
    spec.cameras = ParseCameraSpecs(*cameras);
  }

  return spec;
}
}  // namespace cros
