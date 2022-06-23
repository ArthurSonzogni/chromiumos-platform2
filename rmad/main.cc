// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/logging.h>
#include <base/task/thread_pool/thread_pool_instance.h>
#include <brillo/flag_helper.h>
#include <brillo/syslog_logging.h>

#include "rmad/daemon/dbus_service.h"
#include "rmad/interface/rmad_interface_impl.h"
#include "rmad/minijail/minijail_configuration.h"
#include "rmad/utils/crossystem_utils_impl.h"

namespace {

void CheckWriteProtectAndEnterMinijail() {
  bool set_admin_caps = false;
  rmad::CrosSystemUtilsImpl crossystem_utils;
  int hwwp_status;
  if (crossystem_utils.GetHwwpStatus(&hwwp_status) && hwwp_status == 0) {
    VLOG(1) << "Hardware write protection off.";
    set_admin_caps = true;
  } else {
    VLOG(1) << "Hardware write protection on.";
  }
  rmad::EnterMinijail(set_admin_caps);
}

}  // namespace

int main(int argc, char* argv[]) {
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty);
  brillo::FlagHelper::Init(argc, argv, "Chrome OS RMA Daemon");

  VLOG(1) << "Starting Chrome OS RMA Daemon.";

  CheckWriteProtectAndEnterMinijail();

  base::ThreadPoolInstance::CreateAndStartWithDefaultParams("rmad_thread_pool");

  rmad::RmadInterfaceImpl rmad_interface;
  rmad::DBusService dbus_service(&rmad_interface);

  return dbus_service.Run();
}
