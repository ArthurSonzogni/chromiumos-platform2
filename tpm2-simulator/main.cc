// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tpm2-simulator/simulator.h"

#include <base/at_exit.h>
#include <base/logging.h>
#include <brillo/flag_helper.h>
#include <brillo/syslog_logging.h>

int main(int argc, char* argv[]) {
  DEFINE_bool(sigstop, true, "raise SIGSTOP when TPM initialized");
  DEFINE_string(work_dir, "/mnt/stateful_partition/unencrypted/tpm2-simulator",
                "Daemon data folder");

  base::AtExitManager at_exit;

  brillo::FlagHelper::Init(argc, argv, "TPM2 simulator");
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty);

  if (chdir(FLAGS_work_dir.c_str()) < 0) {
    PLOG(ERROR) << "Failed to change to current directory";
  }

  tpm2_simulator::SimulatorDaemon daemon;
  daemon.set_sigstop_on_initialized(FLAGS_sigstop);

  daemon.Run();
}
