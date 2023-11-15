// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <rmad/utils/futility_utils_impl.h>

#include <array>
#include <memory>
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

}  // namespace

namespace rmad {

FutilityUtilsImpl::FutilityUtilsImpl() {
  cmd_utils_ = std::make_unique<CmdUtilsImpl>();
  hwid_utils_ = std::make_unique<HwidUtilsImpl>();
}

FutilityUtilsImpl::FutilityUtilsImpl(std::unique_ptr<CmdUtils> cmd_utils,
                                     std::unique_ptr<HwidUtils> hwid_utils)
    : cmd_utils_(std::move(cmd_utils)), hwid_utils_(std::move(hwid_utils)) {}

bool FutilityUtilsImpl::GetApWriteProtectionStatus(bool* enabled) {
  std::string futility_output;
  // Get WP status output string.
  if (!cmd_utils_->GetOutput(
          {kFutilityCmd, "flash", "--wp-status", "--ignore-hw"},
          &futility_output)) {
    return false;
  }

  // Check if WP is disabled.
  *enabled = (futility_output.find(kFutilityWriteProtectDisabledStr) ==
              std::string::npos);
  return true;
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

}  // namespace rmad
