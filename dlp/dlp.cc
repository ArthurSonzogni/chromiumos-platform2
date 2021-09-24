// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/logging.h>
#include <brillo/syslog_logging.h>

#include "dlp/dlp_daemon.h"

int main(int /* argc */, char* /* argv */[]) {
  brillo::OpenLog("dlp", true /* log_pid */);
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty);

  // Run daemon.
  LOG(INFO) << "DLP daemon starting";
  dlp::DlpDaemon daemon;
  int result = daemon.Run();
  LOG(INFO) << "DLP daemon stopping with exit code " << result;

  return 0;
}
