// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <brillo/syslog_logging.h>

#include "rgbkbd/dbus_service.h"

int main(int argc, char* argv[]) {
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty);

  LOG(INFO) << "Starting Chrome OS RGB Keyboard Daemon";

  rgbkbd::DBusService dbus_service;
  return dbus_service.Run();
}
