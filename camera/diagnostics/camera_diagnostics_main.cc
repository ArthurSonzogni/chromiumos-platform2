// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/command_line.h>
#include <base/message_loop/message_pump_libevent.h>
#include <brillo/daemons/daemon.h>
#include <brillo/flag_helper.h>
#include <brillo/syslog_logging.h>

#include "diagnostics/camera_diagnostics_mojo_manager.h"
#include "diagnostics/camera_diagnostics_server.h"

static void SetLogItems() {
  constexpr bool kOptionPID = true;
  constexpr bool kOptionTID = true;
  constexpr bool kOptionTimestamp = true;
  constexpr bool kOptionTickcount = true;
  logging::SetLogItems(kOptionPID, kOptionTID, kOptionTimestamp,
                       kOptionTickcount);
}

int main(int argc, char* argv[]) {
  // Init CommandLine for InitLogging.
  base::CommandLine::Init(argc, argv);
  // Enable epoll message pump.
  base::MessagePumpLibevent::InitializeFeatures();

  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty);
  // Override the log items set by brillo::InitLog.
  SetLogItems();
  brillo::FlagHelper::Init(argc, argv, "Camera diagnostics service");

  // Create the daemon instance first to properly set up MessageLoop and
  // AtExitManager.
  brillo::Daemon daemon;

  // This current thread will be considered as IPC thread. Creation and
  // destruction of the IPC objects are safe in this scope.
  cros::CameraDiagnosticsMojoManager mojo_manager;

  cros::CameraDiagnosticsServer camera_diagnostics_server(&mojo_manager);

  LOGF(INFO) << "Starting DAEMON cros-camera-diagnostics service";
  daemon.Run();
  LOGF(INFO) << "End DAEMON cros-camera-diagnostics service";

  return 0;
}
