/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <string>
#include <vector>

#include <base/containers/contains.h>
#include <base/strings/string_util.h>
#include <base/strings/string_number_conversions.h>

#include "cros-camera/common.h"
#include "hal/fake/hal_spec.h"
#include "hal/fake/value_util.h"

namespace cros {

namespace {

constexpr char kCamerasKey[] = "cameras";
constexpr char kIdKey[] = "id";
constexpr char kConnectedKey[] = "connected";
constexpr char kSupportedFormatsKey[] = "supported_formats";
constexpr char kWidthKey[] = "width";
constexpr char kHeightKey[] = "height";
constexpr char kFramesKey[] = "frames";
constexpr char kPathKey[] = "path";

FramesSpec ParseFramesSpec(const DictWithPath& frames_value) {
  if (auto path = GetRequiredValue<std::string>(frames_value, kPathKey)) {
    return FramesFileSpec{base::FilePath(*path)};
  }
  return FramesTestPatternSpec();
}

std::vector<SupportedFormatSpec> ParseSupportedFormatSpecs(
    const ListWithPath& supported_formats_value) {
  std::vector<SupportedFormatSpec> supported_formats;
  // TODO(pihsun): This currently might not satisfy the requirement, since
  // 240p, 480p, 720p might be missing.
  for (const auto& c : supported_formats_value) {
    auto supported_format_value = GetIfDict(c);
    if (!supported_format_value.has_value()) {
      continue;
    }
    SupportedFormatSpec supported_format;
    if (auto width =
            GetRequiredValue<int>(*supported_format_value, kWidthKey)) {
      supported_format.width = *width;
    } else {
      continue;
    }
    if (auto height =
            GetRequiredValue<int>(*supported_format_value, kHeightKey)) {
      supported_format.height = *height;
    } else {
      continue;
    }
    // TODO(pihsun): Support frame rates, actual format.
    supported_formats.push_back(supported_format);
  }
  return supported_formats;
}

std::vector<CameraSpec> ParseCameraSpecs(const ListWithPath& cameras_value) {
  std::vector<CameraSpec> camera_specs;
  for (auto c : cameras_value) {
    auto spec_value = GetIfDict(c);
    if (!spec_value.has_value()) {
      continue;
    }

    CameraSpec camera_spec;

    if (auto id = GetRequiredValue<int>(*spec_value, kIdKey)) {
      if (base::Contains(camera_specs, *id,
                         [](const CameraSpec& spec) { return spec.id; })) {
        LOGF(WARNING) << "duplicated id " << *id << " at " << spec_value->path
                      << ".id, ignore";
        continue;
      }
      camera_spec.id = *id;
    } else {
      // TODO(pihsun): Use generated ID for this case?
      continue;
    }
    camera_spec.connected =
        GetValue<bool>(*spec_value, kConnectedKey).value_or(false);

    if (auto frames = GetValue<DictWithPath>(*spec_value, kFramesKey)) {
      camera_spec.frames = ParseFramesSpec(*frames);
    } else {
      camera_spec.frames = FramesTestPatternSpec();
    }

    if (auto supported_formats =
            GetValue<ListWithPath>(*spec_value, kSupportedFormatsKey)) {
      camera_spec.supported_formats =
          ParseSupportedFormatSpecs(*supported_formats);
      if (camera_spec.supported_formats.empty()) {
        LOGF(WARNING) << "empty supported_formats at "
                      << supported_formats->path << ", ignore";
        continue;
      }
    } else {
      // Using default supported formats.
      // Resolutions are the required ones in
      // https://chromeos.google.com/partner/dlm/docs/latest-requirements/chromebook.html#cam-sw-0003-v01
      camera_spec.supported_formats = {
          {
              .width = 320,
              .height = 240,
          },
          {
              .width = 640,
              .height = 360,
          },
          {
              .width = 640,
              .height = 480,
          },
          {
              .width = 1280,
              .height = 720,
          },
          {
              .width = 1280,
              .height = 960,
          },
          {
              .width = 1920,
              .height = 1080,
          },
      };
    }

    camera_specs.push_back(camera_spec);
  }
  return camera_specs;
}

}  // namespace

std::optional<HalSpec> ParseHalSpecFromJsonValue(
    const base::Value::Dict& value) {
  HalSpec spec;

  DictWithPath root = {&value, {}};

  if (auto cameras = GetRequiredValue<ListWithPath>(root, kCamerasKey)) {
    spec.cameras = ParseCameraSpecs(*cameras);
  }

  return spec;
}
}  // namespace cros
