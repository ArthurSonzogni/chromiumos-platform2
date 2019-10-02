/*
 * Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <fcntl.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <base/at_exit.h>
#include <base/command_line.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <brillo/syslog_logging.h>
#include <mojo/edk/embedder/platform_channel_utils_posix.h>

#include "common/camera_algorithm_adapter.h"
#include "cros-camera/common.h"
#include "cros-camera/constants.h"
#include "cros-camera/ipc_util.h"

int main(int argc, char** argv) {
  static base::AtExitManager exit_manager;
  int kCameraProcessPriority = 0;

  // Set up logging so we can enable VLOGs with -v / --vmodule.
  base::CommandLine::Init(argc, argv);
  logging::LoggingSettings settings;
  settings.logging_dest = logging::LOG_TO_SYSTEM_DEBUG_LOG;
  LOG_ASSERT(logging::InitLogging(settings));

  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty);

  int ret = setpriority(PRIO_PROCESS, 0, kCameraProcessPriority);
  if (ret) {
    LOGF(WARNING) << "Failed to set process priority";
  }

  base::FilePath socket_path(cros::constants::kCrosCameraAlgoSocketPathString);
  // Creat unix socket to receive the adapter token and connection handle
  int fd = -1;
  if (!cros::CreateServerUnixDomainSocket(socket_path, &fd)) {
    LOGF(ERROR) << "CreateSreverUnixDomainSocket failed";
    return EXIT_FAILURE;
  }
  base::ScopedFD socket_fd(fd);
  int flags = HANDLE_EINTR(fcntl(socket_fd.get(), F_GETFL));
  if (flags == -1) {
    PLOGF(ERROR) << "fcntl(F_GETFL)";
    return EXIT_FAILURE;
  }
  if (HANDLE_EINTR(fcntl(socket_fd.get(), F_SETFL, flags & ~O_NONBLOCK)) ==
      -1) {
    PLOGF(ERROR) << "fcntl(F_SETFL) failed to disable O_NONBLOCK";
    return EXIT_FAILURE;
  }

  pid_t pid = 0;
  while (1) {
    VLOGF(1) << "Waiting for incoming connection for " << socket_path.value();
    base::ScopedFD connection_fd(accept(socket_fd.get(), NULL, 0));
    if (!connection_fd.is_valid()) {
      LOGF(ERROR) << "Failed to accept client connect request";
      return EXIT_FAILURE;
    }
    const size_t kTokenLength = 33;
    char recv_buf[kTokenLength] = {0};
    std::deque<mojo::edk::PlatformHandle> platform_handles;
    if (PlatformChannelRecvmsg(mojo::edk::PlatformHandle(connection_fd.get()),
                               recv_buf, sizeof(recv_buf), &platform_handles,
                               true) == 0) {
      LOGF(ERROR) << "Failed to receive message";
      return EXIT_FAILURE;
    }
    if (platform_handles.size() != 1 || !platform_handles.front().is_valid()) {
      LOGF(ERROR) << "Received connection handle is invalid";
      return EXIT_FAILURE;
    }

    VLOGF(1) << "Message from client " << std::string(recv_buf);
    if (pid > 0) {
      kill(pid, SIGTERM);
    }
    pid = fork();
    if (pid == 0) {
      cros::CameraAlgorithmAdapter adapter;
      adapter.Run(std::string(recv_buf),
                  mojo::edk::ScopedPlatformHandle(platform_handles.front()));
      exit(0);
    } else if (pid < 0) {
      LOGF(ERROR) << "Fork failed";
    }
  }

  return 0;
}
