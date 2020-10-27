// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <brillo/syslog_logging.h>

#include "rmad/dbus_service.h"
#include "rmad/rmad_interface_impl.h"

int main(int argc, char* argv[]) {
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty);

  LOG(INFO) << "Starting Chrome OS RMA Daemon";

  rmad::RmadInterfaceImpl rmad_interface;
  rmad::DBusService dbus_service(&rmad_interface);
  return dbus_service.Run();
}
