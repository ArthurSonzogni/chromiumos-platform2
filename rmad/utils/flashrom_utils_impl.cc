// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <rmad/utils/flashrom_utils_impl.h>

#include <memory>
#include <sstream>
#include <string>
#include <utility>

#include <base/files/file_path.h>
#include <base/files/scoped_temp_dir.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <re2/re2.h>

#include "rmad/utils/cmd_utils_impl.h"

namespace {

constexpr char kFlashromCmd[] = "/usr/sbin/flashrom";
constexpr char kFmapDecodeCmd[] = "/usr/sbin/fmap_decode";
constexpr char kTempFirmwareFilePath[] = "fw.bin";
constexpr char kWriteProtectEnabledStr[] = "write protect is enabled.";
constexpr char kWriteProtectRangeRegexp[] =
    R"a(area_offset="(0x[[:xdigit:]]+)"\s*area_size="(0x[[:xdigit:]]+)"\s*)a"
    R"(area_name="WP_RO")";

}  // namespace

namespace rmad {

FlashromUtilsImpl::FlashromUtilsImpl() : FlashromUtils() {
  cmd_utils_ = std::make_unique<CmdUtilsImpl>();
}

FlashromUtilsImpl::FlashromUtilsImpl(std::unique_ptr<CmdUtils> cmd_utils)
    : FlashromUtils(), cmd_utils_(std::move(cmd_utils)) {}

bool FlashromUtilsImpl::GetSoftwareWriteProtectionStatus(bool* enabled) {
  std::string flashrom_output;
  if (!cmd_utils_->GetOutput({kFlashromCmd, "-p", "host", "--wp-status"},
                             &flashrom_output)) {
    return false;
  }
  if (flashrom_output.find(kWriteProtectEnabledStr) != std::string::npos) {
    *enabled = true;
  } else {
    *enabled = false;
  }
  return true;
}

bool FlashromUtilsImpl::EnableSoftwareWriteProtection() {
  int wp_start, wp_len;
  if (!GetWriteProtectionRange(&wp_start, &wp_len)) {
    LOG(ERROR) << "Failed to get write protection range";
    return false;
  }

  std::stringstream wp_range_arg;
  wp_range_arg << wp_start << "," << wp_len;
  std::string unused_output;
  return cmd_utils_->GetOutput({kFlashromCmd, "-p", "host", "--wp-enable",
                                "--wp-range", wp_range_arg.str()},
                               &unused_output);
}

bool FlashromUtilsImpl::DisableSoftwareWriteProtection() {
  std::string unused_output;
  return cmd_utils_->GetOutput({kFlashromCmd, "-p", "host", "--wp-disable"},
                               &unused_output);
}

bool FlashromUtilsImpl::GetWriteProtectionRange(int* wp_start, int* wp_len) {
  base::ScopedTempDir temp_dir_;
  CHECK(temp_dir_.CreateUniqueTempDir());
  const base::FilePath firmware_file_path =
      temp_dir_.GetPath().AppendASCII(kTempFirmwareFilePath);

  std::string unused_output;
  if (!cmd_utils_->GetOutput(
          {kFlashromCmd, "-p", "host", "-r", firmware_file_path.MaybeAsASCII()},
          &unused_output)) {
    LOG(ERROR) << "Failed to read AP firmware";
    return false;
  }

  std::string fmap_output;
  if (!cmd_utils_->GetOutput(
          {kFmapDecodeCmd, firmware_file_path.MaybeAsASCII()}, &fmap_output)) {
    LOG(ERROR) << "Failed to decode fmap";
    return false;
  }

  re2::StringPiece string_piece(fmap_output);
  re2::RE2 regexp(kWriteProtectRangeRegexp);
  std::string wp_start_str, wp_len_str;
  if (!RE2::PartialMatch(string_piece, regexp, &wp_start_str, &wp_len_str)) {
    LOG(ERROR) << "Failed to parse fmap";
    return false;
  }

  return base::HexStringToInt(wp_start_str, wp_start) &&
         base::HexStringToInt(wp_len_str, wp_len);
}

}  // namespace rmad
