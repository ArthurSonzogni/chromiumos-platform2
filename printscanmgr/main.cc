// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unistd.h>

#include <cstdlib>

#include <base/check_op.h>
#include <base/logging.h>
#include <brillo/syslog_logging.h>

#include "printscanmgr/daemon/daemon.h"
#include "printscanmgr/executor/executor.h"
#include "printscanmgr/minijail/minijail_configuration.h"

int main(int arg, char** argv) {
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty);

  // The root-level parent process will continue on as the executor, and the
  // child will become the sandboxed printscanmgr daemon.
  pid_t pid = fork();

  if (pid == -1) {
    PLOG(FATAL) << "Failed to fork";
    return EXIT_FAILURE;
  }

  if (pid == 0) {
    CHECK_EQ(getuid(), 0) << "Executor must run as root";

    printscanmgr::EnterExecutorMinijail();

    return printscanmgr::Executor().Run();
  }

  LOG(INFO) << "Starting printscanmgr daemon.";

  printscanmgr::EnterDaemonMinijail();

  return printscanmgr::Daemon().Run();
}
