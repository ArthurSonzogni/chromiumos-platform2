/*
 * Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <fcntl.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <utility>
#include <vector>

#include <base/at_exit.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/message_loop/message_pump_epoll.h>
#include <brillo/flag_helper.h>
#include <brillo/syslog_logging.h>
#include <mojo/public/cpp/platform/socket_utils_posix.h>

#include "common/camera_algorithm_adapter.h"
#include "common/camera_algorithm_adapter_libcamera.h"
#include "cros-camera/common.h"
#include "cros-camera/constants.h"
#include "cros-camera/device_config.h"
#include "cros-camera/ipc_util.h"

int main(int argc, char** argv) {
  static base::AtExitManager exit_manager;
  int kCameraProcessPriority = 0;
  DEFINE_string(type, "vendor", "Algorithm type, e.g. vendor or gpu");
  brillo::FlagHelper::Init(argc, argv, "Camera algorithm service.");
  if (FLAGS_type != "vendor" && FLAGS_type != "gpu") {
    LOGF(ERROR) << "Invalid type";
    return EXIT_FAILURE;
  }

  // Enable epoll message pump.
  base::MessagePumpEpoll::InitializeFeatures();

  // Set up logging so we can enable VLOGs with -v / --vmodule.
  logging::LoggingSettings settings;
  settings.logging_dest =
      logging::LOG_TO_SYSTEM_DEBUG_LOG | logging::LOG_TO_STDERR;
  LOG_ASSERT(logging::InitLogging(settings));

  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty);

  if (!cros::DeviceConfig::Create()->HasMipiCamera()) {
    LOGF(INFO) << "No MIPI camera so stopping cros-camera-algo";
    // Give cros-camera-algo a hint to stop respawning.
    if (!base::OpenFile(
            base::FilePath(cros::constants::kForceStopCrosCameraAlgoPath),
            "w")) {
      LOGF(ERROR) << "Cannot touch file: "
                  << cros::constants::kForceStopCrosCameraAlgoPath;
      return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
  }

  int ret = setpriority(PRIO_PROCESS, 0, kCameraProcessPriority);
  if (ret) {
    LOGF(WARNING) << "Failed to set process priority";
  }

  base::FilePath socket_file_path(
      (FLAGS_type == "vendor")
#if USE_LIBCAMERA
          ? cros::constants::kCrosCameraAlgoLibcameraSocketPathString
          : cros::constants::kCrosCameraAlgoGpuLibcameraSocketPathString);
#else
          ? cros::constants::kCrosCameraAlgoSocketPathString
          : cros::constants::kCrosCameraGPUAlgoSocketPathString);
#endif
  // Create unix socket to receive the adapter token and connection handle
  int fd = -1;
  if (!cros::CreateServerUnixDomainSocket(socket_file_path, &fd)) {
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

  // Make sure the child process does not become a zombie.
  signal(SIGCHLD, SIG_IGN);

  pid_t pid = 0;
  while (1) {
    VLOGF(1) << "Waiting for incoming connection for "
             << socket_file_path.value();
    base::ScopedFD connection_fd(accept(socket_fd.get(), NULL, 0));
    if (!connection_fd.is_valid()) {
      LOGF(ERROR) << "Failed to accept client connect request";
      return EXIT_FAILURE;
    }
#if !USE_LIBCAMERA
    constexpr size_t kMaxMessageLength = 33;
    char recv_buf[kMaxMessageLength] = {0};
    std::vector<base::ScopedFD> platform_handles;
    if (mojo::SocketRecvmsg(connection_fd.get(), recv_buf, sizeof(recv_buf) - 1,
                            &platform_handles, true) == 0) {
      LOGF(ERROR) << "Failed to receive message";
      return EXIT_FAILURE;
    }
    if (platform_handles.size() != 1 || !platform_handles.front().is_valid()) {
      LOGF(ERROR) << "Received connection handle is invalid";
      return EXIT_FAILURE;
    }

    VLOGF(1) << "Message from client: " << recv_buf;
    if (pid > 0) {
      kill(pid, SIGTERM);
    }
#endif
    pid = fork();
    if (pid == 0) {
#if USE_LIBCAMERA
      cros::CameraAlgorithmAdapterLibcamera adapter;
      adapter.Run(std::move(connection_fd), FLAGS_type == "vendor");
#else
      cros::CameraAlgorithmAdapter adapter;
      adapter.Run(std::string(recv_buf), std::move(platform_handles[0]));
#endif
      exit(0);
    } else if (pid < 0) {
      LOGF(ERROR) << "Fork failed";
    }
  }
}
