// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LOGIN_MANAGER_ARC_DLC_HARDWARE_FILTER_HELPER_H_
#define LOGIN_MANAGER_ARC_DLC_HARDWARE_FILTER_HELPER_H_

#include <optional>
#include <string>
#include <string_view>

#include <base/byte_count.h>

namespace base {
class FilePath;
}  // namespace base

namespace login_manager {

// A class that wraps helper functions for ARC DLC hardware checks.
class ArcDlcHardwareFilterHelper {
 public:
  ArcDlcHardwareFilterHelper() = delete;
  ~ArcDlcHardwareFilterHelper() = delete;

  // Reads and trims a string from a file.
  // Returns std::nullopt on failure.
  static std::optional<std::string> ReadAndTrimString(
      const base::FilePath& file_path);

  // Reads and converts a hex string to a 16-bit unsigned integer.
  // Returns std::nullopt on failure or if the value overflows.
  static std::optional<uint16_t> ReadHexStringToUint16(
      const base::FilePath& path);

  // Reads and converts a hex string to a 32-bit unsigned integer.
  // Returns std::nullopt on failure.
  static std::optional<uint32_t> ReadHexStringToUint32(
      const base::FilePath& path);

  // Reads and converts a string to an integer.
  // Returns std::nullopt on failure.
  static std::optional<int> ReadStringToInt(const base::FilePath& path);

  // Parses the content of /proc/iomem and returns the total system RAM
  // in bytes, rounded up to the nearest GiB. Returns std::nullopt on failure.
  static std::optional<uint64_t> ParseIomemContent(
      const std::string_view content);

  // Gets the PCI class from a 32-bit integer.
  static uint8_t GetPciClass(uint32_t val);
};

}  // namespace login_manager

#endif  // LOGIN_MANAGER_ARC_DLC_HARDWARE_FILTER_HELPER_H_
