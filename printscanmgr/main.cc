// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unistd.h>

#include <cstdlib>
#include <utility>

#include <base/check_op.h>
#include <base/command_line.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <brillo/syslog_logging.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/public/cpp/platform/platform_channel.h>

#include "printscanmgr/daemon/daemon.h"
#include "printscanmgr/executor/executor.h"
#include "printscanmgr/minijail/minijail_configuration.h"

int main(int arg, char** argv) {
  base::CommandLine::Init(arg, argv);
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty);

  // The parent and child processes will each keep one end of this message pipe
  // and use it to bootstrap a Mojo connection to each other.
  mojo::PlatformChannel channel;
  auto printscanmgr_endpoint = channel.TakeLocalEndpoint();
  auto executor_endpoint = channel.TakeRemoteEndpoint();

  // Apply CLOEXEC because PlatformChannel creates the socketpair without it.
  if (!base::SetCloseOnExec(
          printscanmgr_endpoint.platform_handle().GetFD().get()) ||
      !base::SetCloseOnExec(
          executor_endpoint.platform_handle().GetFD().get())) {
    PLOG(FATAL) << "Error calling SetCloseOnExec";
  }

  // The child process will become the executor, and the parent process will
  // continue on as the sandboxed printscanmgr daemon.
  pid_t pid = fork();

  if (pid == -1) {
    PLOG(FATAL) << "Failed to fork";
  }

  if (pid == 0) {
    CHECK_EQ(getuid(), 0) << "Executor must run as root";

    LOG(INFO) << "Starting executor daemon.";

    mojo::core::Init();
    printscanmgr::EnterExecutorMinijail();

    printscanmgr_endpoint.reset();
    return printscanmgr::Executor(std::move(executor_endpoint)).Run();
  }

  LOG(INFO) << "Starting printscanmgr daemon.";

  // Initialize the printscanmgr daemon as the broker in the daemon/executor
  // Mojo network.
  mojo::core::Init(mojo::core::Configuration{.is_broker_process = true});
  printscanmgr::EnterDaemonMinijail();

  executor_endpoint.reset();
  return printscanmgr::Daemon(std::move(printscanmgr_endpoint)).Run();
}
