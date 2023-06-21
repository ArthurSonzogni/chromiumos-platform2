// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <map>
#include <glob.h>
#include <optional>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <linux/fs.h>

#include <base/base64.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/json/json_reader.h>
#include <base/logging.h>
#include <base/strings/string_piece.h>
#include <base/strings/string_split.h>
#include <base/values.h>
#include <brillo/process/process.h>

#include "libsegmentation/device_info.pb.h"
#include "libsegmentation/feature_management_interface.h"
#include "libsegmentation/feature_management_util.h"

namespace segmentation {

// Writes |device_info| as base64 to |file_path|. Returns false if the write
// isn't successful.
std::optional<libsegmentation::DeviceInfo>
FeatureManagementUtil::ReadDeviceInfoFromFile(const base::FilePath& file_path) {
  std::string encoded;
  if (!base::ReadFileToString(file_path, &encoded)) {
    LOG(ERROR) << "Failed to read protobuf string from file: " << file_path;
    return std::nullopt;
  }

  // The value is expected to be in the base64 format.
  std::string decoded;
  base::Base64Decode(encoded, &decoded);
  libsegmentation::DeviceInfo device_info;
  if (!device_info.ParseFromString(decoded)) {
    LOG(ERROR) << "Failed to parse device info from the protobuf";
    return std::nullopt;
  }
  return device_info;
}

bool FeatureManagementUtil::WriteDeviceInfoToFile(
    const libsegmentation::DeviceInfo& device_info,
    const base::FilePath& file_path) {
  std::string serialized = device_info.SerializeAsString();
  std::string base64_encoded;
  base::Base64Encode(serialized, &base64_encoded);
  return base::WriteFile(file_path, base64_encoded);
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

namespace {
std::map<char, std::string> BASE8_MAP{{'2', "000"}, {'3', "001"}, {'4', "010"},
                                      {'5', "011"}, {'6', "100"}, {'7', "101"},
                                      {'8', "110"}, {'9', "111"}};

std::map<char, std::string> BASE32_MAP{
    {'A', "00000"}, {'B', "00001"}, {'C', "00010"}, {'D', "00011"},
    {'E', "00100"}, {'F', "00101"}, {'G', "00110"}, {'H', "00111"},
    {'I', "01000"}, {'J', "01001"}, {'K', "01010"}, {'L', "01011"},
    {'M', "01100"}, {'N', "01101"}, {'O', "01110"}, {'P', "01111"},
    {'Q', "10000"}, {'R', "10001"}, {'S', "10010"}, {'T', "10011"},
    {'U', "10100"}, {'V', "10101"}, {'W', "10110"}, {'X', "10111"},
    {'Y', "11000"}, {'Z', "11001"}, {'2', "11010"}, {'3', "11011"},
    {'4', "11100"}, {'5', "11101"}, {'6', "11110"}, {'7', "11111"}};

}  // namespace

std::optional<std::string> FeatureManagementUtil::DecodeHWID(
    const std::string& hwid) {
  // For instance, assume hwid = "REDRIX-ZZCR D3A-39F-27K-E6B"
  // After removing the prefix, translate the triplet of character using the
  // maps above, the middle character using a smaller map.
  std::string decoded_bit_string;
  std::vector<base::StringPiece> payload = base::SplitStringPiece(
      hwid, " ", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  if (payload.size() != 2)
    return std::nullopt;

  for (const auto& key : base::SplitStringPiece(
           payload[1], "-", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    if (key.size() != 3)
      return std::nullopt;

    decoded_bit_string.append(BASE32_MAP[key[0]]);
    decoded_bit_string.append(BASE8_MAP[key[1]]);
    decoded_bit_string.append(BASE32_MAP[key[2]]);
  }
  if (decoded_bit_string.empty())
    return std::nullopt;

  return decoded_bit_string;
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
      json_string, base::JSON_ALLOW_CONTROL_CHARS);
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
    if (path)
      return path;
  }
  return std::nullopt;
}

}  // namespace segmentation
