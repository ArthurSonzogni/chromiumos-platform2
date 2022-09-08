// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/command_line.h>
#include <base/files/file_path.h>
#include <base/logging.h>
#include <brillo/syslog_logging.h>

#include "oobe_config/metrics.h"
#include "oobe_config/oobe_config.h"

namespace oobe_config {

namespace {

void InitLog() {
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty);
  logging::SetLogItems(true /* enable_process_id */,
                       true /* enable_thread_id */, true /* enable_timestamp */,
                       true /* enable_tickcount */);
}

}  // namespace

}  // namespace oobe_config

int main(int argc, char* argv[]) {
  oobe_config::InitLog();

  oobe_config::Metrics metrics;

  base::CommandLine::Init(argc, argv);
  LOG(INFO) << "Starting oobe_config_save";
  bool save_result = oobe_config::OobeConfig().EncryptedRollbackSave();

  if (!save_result) {
    LOG(ERROR) << "Failed to save rollback data";
    metrics.RecordSaveResult(
        oobe_config::Metrics::RollbackSaveResult::kStage2Failure);
    return 0;
  }

  LOG(INFO) << "Exiting oobe_config_save";
  metrics.RecordSaveResult(oobe_config::Metrics::RollbackSaveResult::kSuccess);
  return 0;
}
