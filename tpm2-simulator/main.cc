// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tpm2-simulator/simulator.h"

#include <base/command_line.h>
#include <base/logging.h>
#include <brillo/syslog_logging.h>

int main(int argc, char* argv[]) {
  // Initialize command line configuration early, as logging will require
  // command line to be initialized
  base::CommandLine::Init(argc, argv);
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty);

  if (argc != 1) {
    LOG(ERROR) << "Program accepts no command line arguments";
    return EXIT_FAILURE;
  }

  tpm2_simulator::SimulatorDaemon daemon;
  daemon.Run();
}
