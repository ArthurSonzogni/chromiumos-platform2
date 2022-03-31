// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/command_line.h>
#include <brillo/syslog_logging.h>

#include "vtpm/commands/null_command.h"
#include "vtpm/vtpm_daemon.h"

int main(int argc, char* argv[]) {
  base::CommandLine::Init(argc, argv);
  base::CommandLine* cl = base::CommandLine::ForCurrentProcess();
  int flags = brillo::kLogToSyslog;
  if (cl->HasSwitch("log_to_stderr")) {
    flags |= brillo::kLogToStderr;
  }
  brillo::InitLog(flags);

  // Currently this is null-implemented, and always returns an empty string.
  // TODO(b/227341806): Implement the commands to be supported in a virtual vtpm
  // implementation.
  vtpm::NullCommand null_command;

  return vtpm::VtpmDaemon(&null_command).Run();
}
