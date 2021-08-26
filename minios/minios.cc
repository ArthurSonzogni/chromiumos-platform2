// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "minios/minios.h"

#include <utility>

#include <base/logging.h>

#include <vboot/crossystem.h>

#include "minios/process_manager.h"
#include "minios/recovery_installer.h"

namespace minios {

const char kDebugConsole[] = "/dev/pts/2";
const char kLogFile[] = "/log/recovery.log";

MiniOs::MiniOs(std::shared_ptr<UpdateEngineProxy> update_engine_proxy,
               std::shared_ptr<NetworkManagerInterface> network_manager)
    : update_engine_proxy_(update_engine_proxy),
      network_manager_(network_manager),
      draw_utils_(std::make_shared<DrawUtils>(&process_manager_)),
      screens_controller_(ScreenController(draw_utils_,
                                           update_engine_proxy_,
                                           network_manager_,
                                           &process_manager_)) {}

int MiniOs::Run() {
  LOG(INFO) << "Starting miniOS.";

  // Only start the shell for debug builds.
  if (VbGetSystemPropertyInt("cros_debug") == 1) {
    StartShell();
  }

  if (!screens_controller_.Init()) {
    LOG(ERROR) << "Screens init failed. Exiting.";
    return 1;
  }

  return 0;
}

void MiniOs::StartShell() {
  // Start the background shell on DEBUG console.
  pid_t shell_pid;
  if (!ProcessManager().RunBackgroundCommand({"/bin/sh"},
                                             ProcessManager::IORedirection{
                                                 .input = kDebugConsole,
                                                 .output = kDebugConsole,
                                             },
                                             &shell_pid)) {
    LOG(ERROR) << "Failed to start shell in the background.";
    return;
  }

  LOG(INFO) << "Started shell in the background as pid: " << shell_pid;
}

bool MiniOs::GetState(State* state_out, brillo::ErrorPtr* error) {
  state_out->CopyFrom(state_);
  return true;
}

}  // namespace minios
