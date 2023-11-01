// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/command_line.h>
#include <base/files/file_path.h>
#include <base/logging.h>
#include <brillo/flag_helper.h>

#include "fbpreprocessor/configuration.h"
#include "fbpreprocessor/fbpreprocessor_daemon.h"

int main(int argc, char* argv[]) {
  DEFINE_string(log_dir, "/var/log/", "Directory where logs are written.");
  DEFINE_uint32(file_expiration,
                fbpreprocessor::Configuration::kDefaultExpirationSeconds,
                "Default expiration period of processed files, in seconds.");

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

  fbpreprocessor::Configuration config;
  int file_expiration = FLAGS_file_expiration;
  // Don't make it possible to have an arbitrary long expiration period with a
  // misconfigured command line.
  if (file_expiration >
      fbpreprocessor::Configuration::kDefaultExpirationSeconds) {
    file_expiration = fbpreprocessor::Configuration::kDefaultExpirationSeconds;
    LOG(ERROR) << "File expiration set to invalid " << FLAGS_file_expiration
               << " seconds, resetting to " << file_expiration << " seconds.";
  }
  LOG(INFO) << "Default file expiration set to " << file_expiration << "s";
  config.set_default_expirations_secs(file_expiration);

  fbpreprocessor::FbPreprocessorDaemon daemon(config);
  return daemon.Run();
}
