// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modemfwd/firmware_file.h"

#include <string>

#include <base/check_op.h>
#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/notreached.h>
#include <brillo/process/process.h>

#include "modemfwd/file_decompressor.h"

namespace modemfwd {

FirmwareFile::FirmwareFile() = default;

FirmwareFile::~FirmwareFile() = default;

bool FirmwareFile::PrepareFrom(const base::FilePath& firmware_dir,
                               const base::FilePath& temp_extraction_dir,
                               const FirmwareFileInfo& file_info) {
  base::FilePath firmware_path = firmware_dir.Append(file_info.firmware_path);
  switch (file_info.compression) {
    case FirmwareFileInfo::Compression::NONE:
      path_for_logging_ = firmware_path;
      path_on_filesystem_ = firmware_path;
      return true;

    case FirmwareFileInfo::Compression::XZ: {
      // A xz-compressed firmware file should end with a .xz extension.
      CHECK_EQ(firmware_path.FinalExtension(), ".xz");

      // Maintains the original firmware file name with the trailing .xz
      // extension removed.
      base::FilePath actual_path = temp_extraction_dir.Append(
          firmware_path.BaseName().RemoveFinalExtension());

      if (!DecompressXzFile(firmware_path, actual_path)) {
        LOG(ERROR) << "Failed to decompress firmware: "
                   << firmware_path.value();
        return false;
      }
      path_for_logging_ = firmware_path;
      path_on_filesystem_ = actual_path;
      return true;
    }
    case FirmwareFileInfo::Compression::BSDIFF: {
      constexpr std::string patchmaker_path("/usr/bin/patchmaker");
      brillo::ProcessImpl cmd;

      path_for_logging_ = firmware_path;
      path_on_filesystem_ = temp_extraction_dir.Append(file_info.firmware_path);

      cmd.AddArg(patchmaker_path);
      cmd.AddArg("--decode");
      cmd.AddArg("--src_path=" + firmware_path.value());
      cmd.AddArg("--dest_path=" + temp_extraction_dir.value());

      return 0 == cmd.Run();
    }
  }

  NOTREACHED_NORETURN();
}

}  // namespace modemfwd
