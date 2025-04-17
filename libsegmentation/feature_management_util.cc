// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libsegmentation/feature_management_util.h"

#include <fcntl.h>
#include <glob.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#include <optional>
#include <utility>

#include <base/base64.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/json/json_reader.h>
#include <base/logging.h>
#include <base/strings/string_split.h>
#include <base/values.h>
#include <brillo/process/process.h>

#include "libsegmentation/device_info.pb.h"
#include "libsegmentation/feature_management_interface.h"

namespace segmentation {

// Writes |device_info| as base64 to |file_path|. Returns false if the write
// isn't successful.
std::optional<libsegmentation::DeviceInfo>
FeatureManagementUtil::ReadDeviceInfo(const std::string& encoded) {
  std::string decoded;
  // The value is expected to be in the base64 format.
  base::Base64Decode(encoded, &decoded);
  libsegmentation::DeviceInfo device_info;
  if (!device_info.ParseFromString(decoded)) {
    LOG(ERROR) << "Failed to parse device info from the protobuf";
    return std::nullopt;
  }
  return device_info;
}

std::optional<libsegmentation::DeviceInfo>
FeatureManagementUtil::ReadDeviceInfo(const base::FilePath& file_path) {
  std::string encoded;
  if (!base::ReadFileToString(file_path, &encoded)) {
    LOG(ERROR) << "Failed to read protobuf string from file: " << file_path;
    return std::nullopt;
  }
  return ReadDeviceInfo(encoded);
}

std::string FeatureManagementUtil::EncodeDeviceInfo(
    const libsegmentation::DeviceInfo& device_info) {
  std::string serialized = device_info.SerializeAsString();
  return base::Base64Encode(serialized);
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
    case libsegmentation::DeviceInfo_FeatureLevel::
        DeviceInfo_FeatureLevel_FEATURE_LEVEL_2:
      return FeatureManagementInterface::FeatureLevel::FEATURE_LEVEL_2;
    default:
      return FeatureManagementInterface::FeatureLevel::FEATURE_LEVEL_UNKNOWN;
  }
}

FeatureManagementInterface::ScopeLevel
FeatureManagementUtil::ConvertProtoScopeLevel(
    libsegmentation::DeviceInfo_ScopeLevel scope_level) {
  switch (scope_level) {
    case libsegmentation::DeviceInfo_ScopeLevel::
        DeviceInfo_ScopeLevel_SCOPE_LEVEL_UNKNOWN:
      return FeatureManagementInterface::ScopeLevel::SCOPE_LEVEL_UNKNOWN;
    case libsegmentation::DeviceInfo_ScopeLevel::
        DeviceInfo_ScopeLevel_SCOPE_LEVEL_0:
      return FeatureManagementInterface::ScopeLevel::SCOPE_LEVEL_0;
    case libsegmentation::DeviceInfo_ScopeLevel::
        DeviceInfo_ScopeLevel_SCOPE_LEVEL_1:
      return FeatureManagementInterface::ScopeLevel::SCOPE_LEVEL_1;
    default:
      return FeatureManagementInterface::ScopeLevel::SCOPE_LEVEL_UNKNOWN;
  }
}

std::optional<int64_t> FeatureManagementUtil::GetDiskSpace(
    const base::FilePath& dev) {
  base::ScopedFD fd(HANDLE_EINTR(
      open(dev.value().c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC)));
  if (!fd.is_valid()) {
    PLOG(ERROR) << "open failed";
    return std::nullopt;
  }

  int64_t size;
  if (ioctl(fd.get(), BLKGETSIZE64, &size)) {
    PLOG(ERROR) << "ioctl(BLKGETSIZE) failed";
    return std::nullopt;
  }
  return size;
}

// TODO(b:176492189): Move to a common library
// Return a dictionary of the image variables defining the partitions
// sizes and offsets on the storage as well as globs to where it
// can be found.
// - root: usually '/'.unless we are unit testing.
// - entry:
//   `load_base_vars`: for fixed storage information
//   'load_partition_vars': for removable [usb install image] information.
//
//
std::optional<base::Value> GetPartitionVars(const base::FilePath& root,
                                            const std::string entry) {
  std::string json_string;
  if (!base::ReadFileToString(root.Append("usr/sbin/partition_vars.json"),
                              &json_string)) {
    PLOG(ERROR) << "Unable to read json file";
    return std::nullopt;
  }
  // JSON_PARSE_RFC (no override) is acceptable for produciong json,
  // but for testing, it is more readable to add '\n' for readability.
  auto part_vars = base::JSONReader::ReadAndReturnValueWithError(
      json_string, base::JSON_ALLOW_NEWLINES_IN_STRINGS);
  if (!part_vars.has_value()) {
    PLOG(ERROR) << "Failed to parse image variables.";
    return std::nullopt;
  }
  if (!part_vars->is_dict()) {
    LOG(ERROR) << "Failed to read json file as a dictionary";
    return std::nullopt;
  }

  base::Value::Dict* image_vars = part_vars->GetDict().FindDict(entry);
  if (image_vars == nullptr) {
    PLOG(ERROR) << "Failed to parse dictionary from partition_vars.json";
    return std::nullopt;
  }
  return base::Value(std::move(*image_vars));
}

// TODO(b:176492189): Move to a common library
std::optional<base::FilePath> FeatureManagementUtil::GetDefaultRoot(
    const base::FilePath& root) {
  std::optional<base::Value> image_vars =
      GetPartitionVars(root, "load_base_vars");

  if (!image_vars) {
    LOG(ERROR) << "Unable to find installation dictionary.";
    return std::nullopt;
  }

  const auto& image_vars_dict = image_vars->GetDict();
  const std::string* default_root_globs =
      image_vars_dict.FindString("DEFAULT_ROOTDEV");

  base::FilePath root_path = root.Append("dev");
  for (const std::string& pattern :
       base::SplitString(*default_root_globs, " ", base::TRIM_WHITESPACE,
                         base::SPLIT_WANT_NONEMPTY)) {
    glob_t glob_result;
    std::optional<base::FilePath> path;

    // Append root for testing purposes. The |pattern| are absolute,
    // so we can not use Append().
    base::FilePath pattern_under_test(root.value() + pattern);

    int ret = glob(pattern_under_test.value().c_str(), GLOB_ONLYDIR,
                   /*errfunc=*/nullptr, &glob_result);
    if (ret == EXIT_SUCCESS && glob_result.gl_pathc == 1) {
      base::FilePath system_root_path(glob_result.gl_pathv[0]);
      path = root_path.Append(system_root_path.BaseName());
    }

    globfree(&glob_result);
    if (path) {
      return path;
    }
  }
  return std::nullopt;
}

}  // namespace segmentation
