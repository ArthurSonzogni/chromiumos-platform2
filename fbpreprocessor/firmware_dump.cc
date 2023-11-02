// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fbpreprocessor/firmware_dump.h"

#include <ostream>

#include <base/files/file_path.h>
#include <base/logging.h>
#include <brillo/files/file_util.h>

namespace {
constexpr char kFirmwareDumpFileExtension[] = ".dmp";
}  // namespace

namespace fbpreprocessor {

FirmwareDump::FirmwareDump(const base::FilePath& path)
    : dmp_file_(path.AddExtension(kFirmwareDumpFileExtension)) {}

base::FilePath FirmwareDump::BaseName() const {
  return dmp_file_.BaseName().RemoveExtension();
}

bool FirmwareDump::Delete() const {
  bool success = brillo::DeleteFile(dmp_file_);
  if (!success) {
    LOG(ERROR) << "Failed to delete firmware dump.";
  }
  return success;
}

std::ostream& operator<<(std::ostream& os, const FirmwareDump& dump) {
  os << dump.BaseName();
  return os;
}

}  // namespace fbpreprocessor
