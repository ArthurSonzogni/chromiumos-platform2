// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/command_line.h>
#include <base/files/file_path.h>
#include <brillo/syslog_logging.h>

#include "oobe_config/oobe_config.h"
#include "oobe_config/rollback_helper.h"

namespace {

void InitLog() {
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty);
  logging::SetLogItems(true /* enable_process_id */,
                       true /* enable_thread_id */, true /* enable_timestamp */,
                       true /* enable_tickcount */);
}

}  // namespace

const char kForce[] = "force";

int main(int argc, char* argv[]) {
  InitLog();
  base::CommandLine::Init(argc, argv);
  base::CommandLine* cl = base::CommandLine::ForCurrentProcess();

  const bool should_save = oobe_config::OobeConfig().ShouldSaveRollbackData();
  const bool force = cl->HasSwitch(kForce);
  if (should_save || force) {
    LOG(INFO) << "Saving rollback data. forced=" << force;
    const bool save_succeeded = oobe_config::PrepareSave(
        base::FilePath(), false /* ignore_permissions_for_testing */);

    if (save_succeeded) {
      LOG_IF(ERROR, !save_succeeded) << "Rollback prepare save failed.";
      return 1;
    } else {
      return 0;
    }
  }

  // No data was saved because there is no rollback.
  return 1;
}
