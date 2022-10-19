// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <brillo/syslog_logging.h>

#include "oobe_config/rollback_constants.h"
#include "oobe_config/rollback_helper.h"

namespace {

void InitLog() {
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty);
  logging::SetLogItems(true /* enable_process_id */,
                       true /* enable_thread_id */, true /* enable_timestamp */,
                       true /* enable_tickcount */);
}

}  // namespace

// Cleans up after a rollback happened by deleting any remaining files.
// Should be called once the device is owned.
int main(int argc, char* argv[]) {
  InitLog();

  LOG(INFO)
      << "OOBE is already complete. Cleaning up restore files if they exist.";
  oobe_config::CleanupRestoreFiles(
      base::FilePath() /* root_path */,
      std::set<std::string>() /* excluded_files */);
  return 0;
}
