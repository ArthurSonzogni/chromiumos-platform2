// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/arc_dlc_hardware_filter_helper.h"

#include <limits>
#include <string>
#include <string_view>

#include <base/byte_count.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>

namespace login_manager {

namespace {

uint8_t GetByte(uint32_t val, int id) {
  return (val >> (id * 8)) & 0xFF;
}

}  // namespace

uint8_t ArcDlcHardwareFilterHelper::GetPciClass(uint32_t val) {
  return GetByte(val, 2);
}

std::optional<std::string> ArcDlcHardwareFilterHelper::ReadAndTrimString(
    const base::FilePath& file_path) {
  std::string buffer;
  if (!base::ReadFileToString(file_path, &buffer)) {
    LOG(ERROR) << "Failed to read string file: " << file_path.value();
    return std::nullopt;
  }
  base::TrimWhitespaceASCII(buffer, base::TRIM_ALL, &buffer);
  return buffer;
}

std::optional<uint16_t> ArcDlcHardwareFilterHelper::ReadHexStringToUint16(
    const base::FilePath& path) {
  auto buffer = ReadAndTrimString(path);
  if (!buffer.has_value()) {
    return std::nullopt;
  }

  uint32_t raw;
  if (!base::HexStringToUInt(buffer.value(), &raw)) {
    LOG(ERROR) << "Failed to convert string to integer from file: "
               << path.value() << " with content: \"" << buffer.value() << "\"";
    return std::nullopt;
  }

  // Check for overflow to ensure the value fits into uint16_t.
  if (raw > std::numeric_limits<uint16_t>::max()) {
    LOG(ERROR) << "Value " << raw << " overflows uint16_t.";
    return std::nullopt;
  }

  return static_cast<uint16_t>(raw);
}

std::optional<uint32_t> ArcDlcHardwareFilterHelper::ReadHexStringToUint32(
    const base::FilePath& path) {
  auto buffer = ReadAndTrimString(path);
  if (!buffer.has_value()) {
    return std::nullopt;
  }

  uint32_t raw;
  if (!base::HexStringToUInt(buffer.value(), &raw)) {
    LOG(ERROR) << "Failed to convert string to integer from file: "
               << path.value() << " with content: \"" << buffer.value() << "\"";
    return std::nullopt;
  }

  return raw;
}

std::optional<int> ArcDlcHardwareFilterHelper::ReadStringToInt(
    const base::FilePath& path) {
  auto buffer = ReadAndTrimString(path);
  if (!buffer.has_value()) {
    return std::nullopt;
  }

  int raw;
  if (!base::StringToInt(buffer.value(), &raw)) {
    LOG(ERROR) << "Failed to convert string to integer from file: "
               << path.value() << " with content: \"" << buffer.value() << "\"";
    return std::nullopt;
  }

  return raw;
}

std::optional<uint64_t> ArcDlcHardwareFilterHelper::ParseIomemContent(
    const std::string_view content) {
  uint64_t total_bytes = 0;
  // /proc/iomem content looks like this:
  // "00001000-0009ffff : System RAM"
  base::StringPairs pairs;
  if (!base::SplitStringIntoKeyValuePairs(content, ':', '\n', &pairs)) {
    LOG(ERROR) << "Incorrectly formatted /proc/iomem";
    return std::nullopt;
  }

  for (const auto& [raw_range, raw_label] : pairs) {
    // Trim leading/trailing whitespaces.
    std::string_view range =
        base::TrimString(raw_range, " ", base::TrimPositions::TRIM_ALL);
    std::string_view label =
        base::TrimString(raw_label, " ", base::TrimPositions::TRIM_ALL);
    if (label != "System RAM") {
      continue;
    }
    auto start_end = base::SplitStringPiece(range, "-", base::TRIM_WHITESPACE,
                                            base::SPLIT_WANT_NONEMPTY);
    if (start_end.size() != 2) {
      LOG(ERROR) << "Incorrectly formatted range: " << range;
      return std::nullopt;
    }
    uint64_t start = 0, end = 0;
    if (!base::HexStringToUInt64(start_end[0], &start) ||
        !base::HexStringToUInt64(start_end[1], &end)) {
      LOG(ERROR) << "Incorrectly formatted range: " << range;
      return std::nullopt;
    }
    uint64_t length = end - start + 1;  // +1 as `end` is inclusive.
    total_bytes += length;
  }

  // |total_bytes| can be 0 if |content| is empty or truncated,
  // which should be treated as an error.
  if (total_bytes == 0) {
    return std::nullopt;
  }

  return total_bytes;
}

}  // namespace login_manager
