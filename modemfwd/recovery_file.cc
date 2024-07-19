// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modemfwd/recovery_file.h"

#include <utility>

#include "modemfwd/firmware_directory.h"
#include "modemfwd/firmware_file.h"
#include "modemfwd/modem_helper.h"

namespace modemfwd {

bool PrepareRecoveryFiles(
    ModemHelper* helper,
    const FirmwareDirectory::Files& files,
    FirmwareDirectory* firmware_dir,
    const base::FilePath temp_extraction_dir,
    std::vector<std::unique_ptr<FirmwareFile>>* recovery_files) {
  // The manifest specified recovery file metadata, so let's ask the helper
  // for additional files specified by the metadata it needs for recovery.
  if (!files.recovery_directory.has_value()) {
    return true;
  }

  auto recovery_dir = std::make_unique<FirmwareFile>();
  if (!recovery_dir->PrepareFrom(firmware_dir->GetFirmwarePath(),
                                 temp_extraction_dir,
                                 *files.recovery_directory)) {
    return false;
  }

  for (const auto& file_path :
       helper->GetRecoveryFileList(recovery_dir->path_on_filesystem())) {
    const FirmwareFileInfo file_info(file_path.value(), "0",
                                     files.recovery_directory->compression);

    auto recovery_file = std::make_unique<FirmwareFile>();
    if (!recovery_file->PrepareFrom(firmware_dir->GetFirmwarePath(),
                                    temp_extraction_dir, file_info)) {
      return false;
    }
    recovery_files->emplace_back(std::move(recovery_file));
  }
  recovery_files->emplace_back(std::move(recovery_dir));

  return true;
}

}  // namespace modemfwd
