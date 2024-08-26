/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <signal.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#include <base/command_line.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/message_loop/message_pump_epoll.h>
#include <brillo/daemons/daemon.h>
#include <brillo/syslog_logging.h>
#include <hardware/hardware.h>

#include "cros-camera/common.h"
#include "hal_adapter/camera_hal_adapter.h"
#include "hal_adapter/camera_hal_server_impl.h"

#if USE_CAMERA_ANGLE_BACKEND
#include <base/task/thread_pool/thread_pool_instance.h>

#include "hal_adapter/camera_angle_backend.h"
#endif  // USE_CAMERA_ANGLE_BACKEND

static void SetLogItems() {
  const bool kOptionPID = true;
  const bool kOptionTID = true;
  const bool kOptionTimestamp = true;
  const bool kOptionTickcount = true;
  logging::SetLogItems(kOptionPID, kOptionTID, kOptionTimestamp,
                       kOptionTickcount);
}

int main(int argc, char* argv[]) {
  // Init CommandLine for InitLogging.
  base::CommandLine::Init(argc, argv);
  // Enable epoll message pump.
  base::MessagePumpEpoll::InitializeFeatures();
  int kCameraProcessPriority = 0;

  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty);
  // Override the log items set by brillo::InitLog.
  SetLogItems();

  int ret = setpriority(PRIO_PROCESS, 0, kCameraProcessPriority);
  if (ret) {
    LOGF(WARNING) << "Failed to set process priority";
  }

  // Create the daemon instance first to properly set up MessageLoop and
  // AtExitManager.
  brillo::Daemon daemon;

  cros::CameraHalServerImpl service_provider;
  service_provider.Start();

#if USE_CAMERA_ANGLE_BACKEND
  base::ThreadPoolInstance::CreateAndStartWithDefaultParams("CameraThreadPool");
  cros::FetchAngleStateAndSetupListener();
#endif  // USE_CAMERA_ANGLE_BACKEND

  // The process runs until an error happens which will terminate the process.
  LOGF(INFO) << "Started camera HAL v3 adapter";
  daemon.Run();
  LOGF(ERROR) << "cros-camera daemon stopped";
  return 0;
}
