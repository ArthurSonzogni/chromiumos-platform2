// Copyright 2013 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/files/file_path.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <brillo/syslog_logging.h>

#include "lorgnette/daemon.h"
#include "lorgnette/debug_log.h"

int main(int argc, char** argv) {
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty |
                  brillo::kLogHeader);

  lorgnette::DebugLogManager logman;
  if (logman.SetupDebugging()) {
    LOG(INFO) << "Enabled extra logging for " << argv[0];
  }

  lorgnette::Daemon daemon;

  daemon.Run();

  return 0;
}
