// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MODEMFWD_RECOVERY_FILE_H_
#define MODEMFWD_RECOVERY_FILE_H_

#include <memory>
#include <vector>

#include "modemfwd/firmware_directory.h"
#include "modemfwd/firmware_file.h"
#include "modemfwd/modem_helper.h"

namespace modemfwd {

bool PrepareRecoveryFiles(
    ModemHelper* helper,
    const FirmwareDirectory::Files& files,
    FirmwareDirectory* firmware_dir,
    const base::FilePath temp_extraction_dir,
    std::vector<std::unique_ptr<FirmwareFile>>* recovery_files);

}

#endif  // MODEMFWD_RECOVERY_FILE_H_
