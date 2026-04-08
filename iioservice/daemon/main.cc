// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <atomic>
#include <csignal>

#include <base/command_line.h>
#include <base/logging.h>
#include <brillo/syslog_logging.h>

#include "iioservice/daemon/daemon.h"
#include "iioservice/include/common.h"

int main(int argc, char** argv) {
  base::CommandLine::Init(argc, argv);
  brillo::OpenLog("iioservice", true /*log_pid*/);

  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogHeader);

  LOGF(INFO) << "Daemon started";
  iioservice::Daemon daemon;
  daemon.Run();
  LOGF(INFO) << "Daemon stopped";

  return 0;
}
