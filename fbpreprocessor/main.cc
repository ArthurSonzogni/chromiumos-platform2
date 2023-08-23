// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sysexits.h>

#include <base/command_line.h>
#include <base/files/file_path.h>
#include <base/logging.h>
#include <brillo/flag_helper.h>

#include "fbpreprocessor/fbpreprocessor_daemon.h"

int main(int argc, char* argv[]) {
  DEFINE_string(log_dir, "/var/log/", "Directory where logs are written.");

  brillo::FlagHelper::Init(
      argc, argv, "fbpreprocessord, the debug data preprocessing daemon.");

  const base::FilePath log_file =
      base::FilePath(FLAGS_log_dir).Append("fbpreprocessord.log");

  logging::LoggingSettings logging_settings;
  logging_settings.logging_dest = logging::LOG_TO_FILE;
  logging_settings.log_file_path = log_file.value().c_str();
  logging_settings.lock_log = logging::DONT_LOCK_LOG_FILE;
  logging::InitLogging(logging_settings);

  LOG(INFO) << "Starting fbpreprocessord.";

  fbpreprocessor::FbPreprocessorDaemon daemon;
  int rc = daemon.Run();
  return rc == EX_UNAVAILABLE ? EX_OK : rc;
}
