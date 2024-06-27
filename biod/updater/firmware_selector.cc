// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "biod/updater/firmware_selector.h"

#include <string>
#include <utility>

#include <base/files/file_enumerator.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/notreached.h>
#include <base/types/expected.h>
#include <brillo/files/file_util.h>
#include <brillo/file_utils.h>

namespace {

constexpr char kFirmwareGlobSuffix[] = "_*.bin";
constexpr char kBetaFirmwareSubdir[] = "beta";
constexpr char kAllowBetaFirmwareFile[] = ".allow_beta_firmware";

}  // namespace

namespace biod {
namespace updater {

using FindFirmwareFileStatus = FirmwareSelector::FindFirmwareFileStatus;

bool FirmwareSelector::IsBetaFirmwareAllowed() const {
  return base::PathExists(base_path_.Append(kAllowBetaFirmwareFile));
}

void FirmwareSelector::AllowBetaFirmware(bool enable) {
  base::FilePath beta_firmware_file = base_path_.Append(kAllowBetaFirmwareFile);

  if (enable) {
    // Create file that will indicate the beta firmware can be used.
    brillo::TouchFile(beta_firmware_file);
  } else {
    brillo::DeleteFile(beta_firmware_file);
  }
}

base::expected<base::FilePath, FirmwareSelector::FindFirmwareFileStatus>
FirmwareSelector::FindFirmwareFile(const std::string& board_name) {
  if (IsBetaFirmwareAllowed()) {
    LOG(INFO) << "Trying to find beta firmware file for " << board_name << ".";

    auto status = FindFirmwareFileAtDir(
        firmware_dir_.Append(kBetaFirmwareSubdir), board_name);
    if (status.has_value()) {
      return status;
    }

    LOG(INFO) << "Failed to find beta firmware: "
              << FindFirmwareFileStatusToString(status.error()) << " "
              << "Fallback to production firmware.";
  }

  return FindFirmwareFileAtDir(firmware_dir_, board_name);
}

base::expected<base::FilePath, FirmwareSelector::FindFirmwareFileStatus>
FirmwareSelector::FindFirmwareFileAtDir(const base::FilePath& directory,
                                        const std::string& board_name) {
  if (!base::DirectoryExists(directory)) {
    return base::unexpected(FindFirmwareFileStatus::kNoDirectory);
  }

  std::string glob(board_name + std::string(kFirmwareGlobSuffix));
  base::FileEnumerator fw_bin_list(directory, false,
                                   base::FileEnumerator::FileType::FILES, glob);

  // Find provided firmware file
  base::FilePath fw_bin = fw_bin_list.Next();
  if (fw_bin.empty()) {
    return base::unexpected(FindFirmwareFileStatus::kFileNotFound);
  }
  LOG(INFO) << "Found firmware file '" << fw_bin.value() << "'.";

  // Ensure that there are no other firmware files
  bool extra_fw_files = false;
  for (base::FilePath fw_extra = fw_bin_list.Next(); !fw_extra.empty();
       fw_extra = fw_bin_list.Next()) {
    extra_fw_files = true;
    LOG(ERROR) << "Found firmware file '" << fw_extra.value() << "'.";
  }
  if (extra_fw_files) {
    return base::unexpected(FindFirmwareFileStatus::kMultipleFiles);
  }

  return base::ok(std::move(fw_bin));
}

std::string FirmwareSelector::FindFirmwareFileStatusToString(
    FindFirmwareFileStatus status) {
  switch (status) {
    case FindFirmwareFileStatus::kNoDirectory:
      return "Firmware directory does not exist.";
    case FindFirmwareFileStatus::kFileNotFound:
      return "Firmware file not found.";
    case FindFirmwareFileStatus::kMultipleFiles:
      return "More than one firmware file was found.";
  }

  NOTREACHED();
  return "Unknown find firmware file status encountered.";
}

}  // namespace updater
}  // namespace biod
