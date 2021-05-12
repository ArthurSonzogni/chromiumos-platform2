// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hps/daemon/hps_daemon.h"

#include <base/logging.h>
#include <base/task/thread_pool/thread_pool_instance.h>

#include <brillo/flag_helper.h>
#include <brillo/syslog_logging.h>

int main(int argc, char* argv[]) {
  brillo::FlagHelper::Init(argc, argv, "hps_daemon - HPS services daemon");

  // Always log to syslog and log to stderr if we are connected to a tty.
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty);

  base::ThreadPoolInstance::CreateAndStartWithDefaultParams(
      "hps_daemon_thread_pool");

  LOG(INFO) << "Starting HPS Service.";
  int exit_code = hps::HpsDaemon().Run();
  LOG(INFO) << "HPS Service ended with exit_code=" << exit_code;

  return exit_code;
}
