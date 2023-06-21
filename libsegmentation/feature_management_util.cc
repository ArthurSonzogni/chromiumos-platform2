// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <map>
#include <optional>
#include <vector>

#include <base/base64.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_piece.h>
#include <base/strings/string_split.h>
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

}  // namespace segmentation
