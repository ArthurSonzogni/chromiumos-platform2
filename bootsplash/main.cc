// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/logging.h>
#include <brillo/flag_helper.h>

#include "bootsplash/bootsplash_daemon.h"

int main(int argc, char* argv[]) {
  DEFINE_int32(feature_simon_enabled, 0,
               "The device has the feature 'simon' enabled.");
  brillo::FlagHelper::Init(
      argc, argv, "bootsplash, the Chromium OS boot splash screen manager.");

  logging::LoggingSettings logging_settings;
  logging_settings.logging_dest =
      logging::LOG_TO_SYSTEM_DEBUG_LOG | logging::LOG_TO_STDERR;
  logging_settings.lock_log = logging::DONT_LOCK_LOG_FILE;
  logging::InitLogging(logging_settings);
  logging::SetLogItems(true,   /* process ID */
                       true,   /* thread ID */
                       true,   /* timestamp */
                       false); /* tickcount */

  base::AtExitManager at_exit_manager;

  LOG(INFO) << "Running bootsplash daemon.";
  bootsplash::BootSplashDaemon bootSplashDaemon(FLAGS_feature_simon_enabled);
  int status = bootSplashDaemon.Run();
  if (status) {
    LOG(ERROR) << "Failed to run daemon: status = " << status;
  }

  LOG(INFO) << "bootsplash completed.";
  return 0;
}
