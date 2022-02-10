// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/logging.h>
#include <brillo/flag_helper.h>
#include <brillo/syslog_logging.h>
#include <mojo/core/embedder/embedder.h>

#include "mojo_service_manager/daemon/daemon.h"

int main(int argc, char* argv[]) {
  // Flags are subject to change
  DEFINE_int32(log_level, 0,
               "Logging level - 0: LOG(INFO), 1: LOG(WARNING), 2: LOG(ERROR), "
               "-1: VLOG(1), -2: VLOG(2), ...");

  brillo::FlagHelper::Init(argc, argv, "ChromeOS mojo service manager.");

  brillo::InitLog(brillo::kLogToStderr | brillo::kLogToSyslog);
  logging::SetMinLogLevel(FLAGS_log_level);

  mojo::core::Init(mojo::core::Configuration{.is_broker_process = true});
  return chromeos::mojo_service_manager::Daemon().Run();
}
