// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/utils/futility_utils_impl.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <re2/re2.h>

#include "rmad/utils/cmd_utils_impl.h"
#include "rmad/utils/futility_utils.h"
#include "rmad/utils/hwid_utils_impl.h"

namespace {

// MTD path for checking flash information on arm platform.
constexpr char kMtdPath[] = "/sys/class/mtd/mtd0/device/spi-nor";

constexpr char kFutilityCmd[] = "/usr/bin/futility";
constexpr char kFutilityWriteProtectDisabledStr[] = "WP status: disabled";
constexpr std::array<std::string_view, 5> kSetHwidArgv = {
    kFutilityCmd, "gbb", "--set", "--flash", "--hwid"};

// The format specifier of futility flash size is `%#010x`.
constexpr char kFutilityFlashSizeRegexp[] =
    R"(Flash size: 0x([[:xdigit:]]{8}))";
constexpr char kFutilityFlashNameRegexp[] = R"(Flash name: (.+)\n)";
constexpr char kFutilityFlashWpsrRangeRegexp[] =
    R"(\(start = (\w+), length = (\w+)\))";

}  // namespace

namespace rmad {

FutilityUtilsImpl::FutilityUtilsImpl() {
  cmd_utils_ = std::make_unique<CmdUtilsImpl>();
  hwid_utils_ = std::make_unique<HwidUtilsImpl>();
  mtd_path_ = base::FilePath(kMtdPath);
}

FutilityUtilsImpl::FutilityUtilsImpl(std::unique_ptr<CmdUtils> cmd_utils,
                                     std::unique_ptr<HwidUtils> hwid_utils,
                                     base::FilePath mtd_path)
    : cmd_utils_(std::move(cmd_utils)),
      hwid_utils_(std::move(hwid_utils)),
      mtd_path_(mtd_path) {}

std::optional<bool> FutilityUtilsImpl::GetApWriteProtectionStatus() {
  std::string futility_output;
  // Get WP status output string.
  if (!cmd_utils_->GetOutput(
          {kFutilityCmd, "flash", "--wp-status", "--ignore-hw"},
          &futility_output)) {
    return std::nullopt;
  }

  // Check if WP is disabled.
  return (futility_output.find(kFutilityWriteProtectDisabledStr) ==
          std::string::npos);
}

bool FutilityUtilsImpl::EnableApSoftwareWriteProtection() {
  // Enable AP WP.
  if (std::string output;
      !cmd_utils_->GetOutput({kFutilityCmd, "flash", "--wp-enable"}, &output)) {
    LOG(ERROR) << "Failed to enable AP SWWP";
    LOG(ERROR) << output;
    return false;
  }

  return true;
}

bool FutilityUtilsImpl::DisableApSoftwareWriteProtection() {
  // Disable AP WP.
  if (std::string output; !cmd_utils_->GetOutput(
          {kFutilityCmd, "flash", "--wp-disable"}, &output)) {
    LOG(ERROR) << "Failed to disable AP SWWP";
    LOG(ERROR) << output;
    return false;
  }

  return true;
}

bool FutilityUtilsImpl::SetHwid(const std::string& hwid) {
  if (!hwid_utils_->VerifyHwidFormat(hwid, true)) {
    LOG(ERROR) << "The given HWID has a invalid format.";
    return false;
  }

  if (!hwid_utils_->VerifyChecksum(hwid)) {
    LOG(ERROR) << "The checksum of the given HWID is incorrect.";
    return false;
  }

  std::vector<std::string> argv{kSetHwidArgv.begin(), kSetHwidArgv.end()};
  argv.push_back(hwid);

  std::string output;
  if (!cmd_utils_->GetOutputAndError(argv, &output)) {
    LOG(ERROR) << "Failed to set HWID: " << output;
    return false;
  }

  return true;
}

std::optional<uint64_t> FutilityUtilsImpl::GetFlashSize() {
  std::string output;
  if (!cmd_utils_->GetOutputAndError({kFutilityCmd, "flash", "--flash-size"},
                                     &output)) {
    LOG(ERROR) << "Failed to get flash size: " << output;
    return std::nullopt;
  }

  std::string size_string;
  re2::StringPiece string_piece(output);
  re2::RE2 regexp(kFutilityFlashSizeRegexp);
  if (!RE2::PartialMatch(string_piece, regexp, &size_string)) {
    LOG(ERROR) << "Failed to parse flash size output.";
    LOG(ERROR) << "Flash size output: " << output;
    return std::nullopt;
  }

  uint64_t size;
  if (!base::HexStringToUInt64(size_string, &size)) {
    LOG(ERROR) << "Failed to convert hexadecimal string to integer.";
    LOG(ERROR) << "Hex string: " << output;
    return std::nullopt;
  }

  return size;
}

std::optional<FlashInfo> FutilityUtilsImpl::GetFlashInfo() {
  std::string output;
  if (!cmd_utils_->GetOutputAndError({kFutilityCmd, "flash", "--flash-info"},
                                     &output)) {
    LOG(ERROR) << "Failed to get flash info: " << output;
    return std::nullopt;
  }

  auto flash_name = ParseFlashName(output);
  if (!flash_name.has_value()) {
    return std::nullopt;
  }

  auto flash_range = ParseFlashWpsrRange(output);
  if (!flash_range.has_value()) {
    return std::nullopt;
  }

  return FlashInfo{.flash_name = flash_name.value(),
                   .wpsr_start = flash_range.value().first,
                   .wpsr_length = flash_range.value().second};
}

std::optional<std::string> FutilityUtilsImpl::ParseFlashName(
    const std::string& flash_info_string) {
  std::string name_string;
  if (!RE2::PartialMatch(flash_info_string, kFutilityFlashNameRegexp,
                         &name_string)) {
    LOG(ERROR) << "Failed to parse flash name.";
    LOG(ERROR) << "Flash info string: " << flash_info_string;
    return std::nullopt;
  }

  // In the arm platform, we use the linux MTD driver for the spi nor flash. On
  // the other hand, the flashrom doesn't support the flashinfo query because
  // the linux MTD driver doesn't support it. As a result, we added a debugfs
  // under the MTD_PATH for querying the partid and partname
  if (name_string == "Opaque flash chip") {
    DLOG(INFO) << "Checking flash name via MTD_PATH.";
    base::FilePath partname_path = mtd_path_.AppendASCII("partname");
    std::string partname;
    if (!base::ReadFileToString(partname_path, &partname)) {
      LOG(ERROR) << "Failed to read flash chip partname.";
      return std::nullopt;
    }
    return static_cast<std::string>(
        base::TrimWhitespaceASCII(partname, base::TRIM_TRAILING));
  }

  return name_string;
}

std::optional<std::pair<uint64_t, uint64_t>>
FutilityUtilsImpl::ParseFlashWpsrRange(const std::string& flash_info_string) {
  std::string start_string, length_string;
  if (!RE2::PartialMatch(flash_info_string, kFutilityFlashWpsrRangeRegexp,
                         &start_string, &length_string)) {
    LOG(ERROR) << "Failed to parse flash WPSR range.";
    LOG(ERROR) << "Flash info string: " << flash_info_string;
    return std::nullopt;
  }

  uint64_t start, length;
  if (!base::HexStringToUInt64(start_string, &start) ||
      !base::HexStringToUInt64(length_string, &length)) {
    LOG(ERROR) << "Failed to convert hexadecimal strings to integers.";
    LOG(ERROR) << "Start string: " << start_string
               << ", Length string: " << length_string;
    return std::nullopt;
  }

  return std::make_pair(start, length);
}

}  // namespace rmad
