// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/system/usb_device_info.h"

#include <utility>
#include <vector>

#include <base/check.h>
#include <base/logging.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>

#include "diagnostics/base/file_utils.h"

namespace diagnostics {

USBDeviceInfo::USBDeviceInfo() {
  RetrieveFromFile(GetRootDir().Append(kRelativeUSBDeviceInfoFile));
}

USBDeviceInfo::~USBDeviceInfo() = default;

void USBDeviceInfo::SetEntriesForTesting(
    std::map<std::string, DeviceType> entries) {
  entries_ = std::move(entries);
}

DeviceType USBDeviceInfo::GetDeviceMediaType(
    const std::string& vendor_id, const std::string& product_id) const {
  CHECK(!vendor_id.empty()) << "Invalid vendor ID";
  CHECK(!product_id.empty()) << "Invalid product ID";

  std::string id = vendor_id + ":" + product_id;
  if (entries_.contains(id)) {
    return entries_.at(id);
  }
  return DeviceType::kUSB;
}

void USBDeviceInfo::RetrieveFromFile(const base::FilePath& path) {
  std::string file_content;
  if (!base::ReadFileToString(path, &file_content)) {
    LOG(ERROR) << "Cannot open USB database " << path;
    return;
  }

  auto lines = base::SplitString(file_content, "\n", base::KEEP_WHITESPACE,
                                 base::SPLIT_WANT_NONEMPTY);
  for (const auto& line : lines) {
    if (IsLineSkippable(line))
      continue;

    std::vector<std::string> tokens = base::SplitString(
        line, " ", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
    if (tokens.size() >= 2) {
      entries_.emplace(tokens[0], ConvertToDeviceMediaType(tokens[1]));
    }
  }
  return;
}

DeviceType USBDeviceInfo::ConvertToDeviceMediaType(
    const std::string& str) const {
  if (str == "sd") {
    return DeviceType::kSD;
  } else if (str == "mobile") {
    return DeviceType::kMobile;
  } else {
    return DeviceType::kUSB;
  }
}

bool USBDeviceInfo::IsLineSkippable(const std::string& line) const {
  std::string trimmed_line;
  // Trim only ASCII whitespace for now.
  base::TrimWhitespaceASCII(line, base::TRIM_ALL, &trimmed_line);
  return trimmed_line.empty() ||
         base::StartsWith(trimmed_line, "#", base::CompareCase::SENSITIVE);
}

}  // namespace diagnostics
