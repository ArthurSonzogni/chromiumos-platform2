// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <google/protobuf/util/json_util.h>
#include <libsegmentation/device_info.pb.h>

#include <optional>

#include "libsegmentation/feature_management_interface.h"
#include "libsegmentation/feature_management_util.h"

namespace segmentation {

// Writes |device_info| as JSON to |file_path|. Returns false if the write isn't
// successful.
std::optional<libsegmentation::DeviceInfo>
FeatureManagementUtil::ReadDeviceInfoFromFile(const base::FilePath& file_path) {
  std::string json_input;
  if (!base::ReadFileToString(file_path, &json_input)) {
    LOG(ERROR) << "Failed to read JSON string from file: " << file_path;
    return std::nullopt;
  }

  libsegmentation::DeviceInfo device_info;
  google::protobuf::util::JsonParseOptions options;
  options.ignore_unknown_fields = false;
  auto status = google::protobuf::util::JsonStringToMessage(
      json_input, &device_info, options);
  if (!status.ok()) {
    LOG(ERROR) << "Failed to parse JSON string into DeviceInfo message: "
               << status.message();
    return std::nullopt;
  }

  return device_info;
}

// Writes |device_info| as JSON to |file_path|. Returns false if the write isn't
// successful.
bool FeatureManagementUtil::WriteDeviceInfoToFile(
    const libsegmentation::DeviceInfo device_info,
    const base::FilePath& file_path) {
  std::string json_output;
  google::protobuf::util::JsonPrintOptions options;
  options.add_whitespace = true;
  options.always_print_primitive_fields = true;
  auto status = google::protobuf::util::MessageToJsonString(
      device_info, &json_output, options);
  if (!status.ok()) {
    return false;
  }

  return base::WriteFile(file_path, json_output);
}

FeatureManagementInterface::FeatureLevel
FeatureManagementUtil::ConvertProtoFeatureLevel(
    libsegmentation::DeviceInfo_FeatureLevel feature_level) {
  switch (feature_level) {
    case libsegmentation::DeviceInfo_FeatureLevel::
        DeviceInfo_FeatureLevel_FEATURE_LEVEL_UNKNOWN:
      return FeatureManagementInterface::FeatureLevel::FEATURE_LEVEL_UNKNOWN;
    case libsegmentation::DeviceInfo_FeatureLevel::
        DeviceInfo_FeatureLevel_FEATURE_LEVEL_0:
      return FeatureManagementInterface::FeatureLevel::FEATURE_LEVEL_0;
    case libsegmentation::DeviceInfo_FeatureLevel::
        DeviceInfo_FeatureLevel_FEATURE_LEVEL_1:
      return FeatureManagementInterface::FeatureLevel::FEATURE_LEVEL_1;
    default:
      return FeatureManagementInterface::FeatureLevel::FEATURE_LEVEL_UNKNOWN;
  }
}

}  // namespace segmentation
