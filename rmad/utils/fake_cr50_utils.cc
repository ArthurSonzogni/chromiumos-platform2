// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <rmad/utils/fake_cr50_utils.h>

#include <string>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <brillo/file_utils.h>

#include "rmad/constants.h"

namespace {

constexpr char kDefaultChallengeCode[] = "ABCDEFG";
constexpr char kDefaultUnlockCode[] = "AAAAAAAA";
constexpr char kDefaultBoardIdType[] = "5a5a4352";   // ZZCR.
constexpr char kDefaultBoardIdFlags[] = "00007f80";  // PVT.

}  // namespace

namespace rmad {
namespace fake {

FakeCr50Utils::FakeCr50Utils(const base::FilePath working_dir_path)
    : Cr50Utils(), working_dir_path_(working_dir_path) {}

bool FakeCr50Utils::GetRsuChallengeCode(std::string* challenge_code) const {
  *challenge_code = kDefaultChallengeCode;
  return true;
}

bool FakeCr50Utils::PerformRsu(const std::string& unlock_code) const {
  if (unlock_code == kDefaultUnlockCode) {
    // We don't clear |block_ccd| file if it exists because it doesn't matter.
    base::FilePath factory_mode_enabled_file_path =
        working_dir_path_.AppendASCII(kFactoryModeEnabledFilePath);
    if (!base::PathExists(factory_mode_enabled_file_path)) {
      brillo::TouchFile(factory_mode_enabled_file_path);
    }
    return true;
  } else {
    return false;
  }
}

bool FakeCr50Utils::EnableFactoryMode() const {
  // Factory mode is already enabled.
  if (IsFactoryModeEnabled()) {
    return true;
  }
  // Enable factory mode successfully if HWWP is disabled, and CCD is not
  // blocked by policy.
  const base::FilePath hwwp_disabled_file_path =
      working_dir_path_.AppendASCII(kHwwpDisabledFilePath);
  const base::FilePath block_ccd_file_path =
      working_dir_path_.AppendASCII(kBlockCcdFilePath);
  if (base::PathExists(hwwp_disabled_file_path) &&
      !base::PathExists(block_ccd_file_path)) {
    const base::FilePath factory_mode_enabled_file_path =
        working_dir_path_.AppendASCII(kFactoryModeEnabledFilePath);
    const base::FilePath reboot_request_file_path =
        working_dir_path_.AppendASCII(kRebootRequestFilePath);
    brillo::TouchFile(factory_mode_enabled_file_path);
    brillo::TouchFile(reboot_request_file_path);
    return true;
  }
  return false;
}

bool FakeCr50Utils::DisableFactoryMode() const {
  // Always succeeds.
  if (IsFactoryModeEnabled()) {
    const base::FilePath factory_mode_enabled_file_path =
        working_dir_path_.AppendASCII(kFactoryModeEnabledFilePath);
    base::DeleteFile(factory_mode_enabled_file_path);
  }
  return true;
}

bool FakeCr50Utils::IsFactoryModeEnabled() const {
  const base::FilePath factory_mode_enabled_file_path =
      working_dir_path_.AppendASCII(kFactoryModeEnabledFilePath);
  return base::PathExists(factory_mode_enabled_file_path);
}

bool FakeCr50Utils::GetBoardIdType(std::string* board_id_type) const {
  *board_id_type = kDefaultBoardIdType;
  return true;
}

bool FakeCr50Utils::GetBoardIdFlags(std::string* board_id_flags) const {
  *board_id_flags = kDefaultBoardIdFlags;
  return true;
}

bool FakeCr50Utils::SetBoardId(bool is_custom_label) const {
  return true;
}

}  // namespace fake
}  // namespace rmad
