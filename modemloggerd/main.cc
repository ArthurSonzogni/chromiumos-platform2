// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/logging.h>
#include <brillo/flag_helper.h>
#include <brillo/syslog_logging.h>

#include "modemloggerd/daemon.h"

int main(int argc, char** argv) {
  DEFINE_int32(log_level, 0,
               "Logging level - 0: LOG(INFO), 1: LOG(WARNING), 2: LOG(ERROR), "
               "-1: VLOG(1), -2: VLOG(2), ...");
  brillo::FlagHelper::Init(argc, argv, "Chromium OS Modemlogger daemon");
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty);
  logging::SetMinLogLevel(FLAGS_log_level);
  modemloggerd::Daemon daemon;
  return daemon.Run();
}
