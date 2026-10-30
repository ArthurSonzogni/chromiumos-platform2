// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BIOD_UPDATER_UPDATE_UTILS_H_
#define BIOD_UPDATER_UPDATE_UTILS_H_

#include <string>

#include <base/files/file_path.h>

#include "biod/biod_system.h"

namespace biod {
namespace updater {

std::string UpdaterVersion();

// Checks for external firmware disable mechanism.
bool UpdateDisallowed(const BiodSystem& system);

// Initializes logging to a timestamped bio_fw_updater log file in
// |log_dir_path| and updates the bio_fw_updater.LATEST and
// bio_fw_updater.PREVIOUS symlinks. If |log_dir_path| does not exist, falls
// back to syslog and stderr logging.
void SetupLogging(const base::FilePath& log_dir_path);

}  // namespace updater
}  // namespace biod

#endif  // BIOD_UPDATER_UPDATE_UTILS_H_
