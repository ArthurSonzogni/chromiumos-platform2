// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <rmad/utils/futility_utils_impl.h>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/scoped_temp_dir.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <re2/re2.h>

#include "rmad/utils/cmd_utils_impl.h"
#include "rmad/utils/hwid_utils_impl.h"

namespace {

constexpr char kFutilityCmd[] = "/usr/bin/futility";
constexpr char kFutilityWriteProtectDisabledStr[] = "WP status: disabled";
constexpr std::array<std::string_view, 5> kSetHwidArgv = {
    kFutilityCmd, "gbb", "--set", "--flash", "--hwid"};

// The format specifier of futility flash size is `%#010x`.
constexpr char kFutilityFlashSizeRegexp[] =
    R"(Flash size: 0x([[:xdigit:]]{8}))";

}  // namespace

namespace rmad {

FutilityUtilsImpl::FutilityUtilsImpl() {
  cmd_utils_ = std::make_unique<CmdUtilsImpl>();
  hwid_utils_ = std::make_unique<HwidUtilsImpl>();
}

FutilityUtilsImpl::FutilityUtilsImpl(std::unique_ptr<CmdUtils> cmd_utils,
                                     std::unique_ptr<HwidUtils> hwid_utils)
    : cmd_utils_(std::move(cmd_utils)), hwid_utils_(std::move(hwid_utils)) {}

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

}  // namespace rmad
