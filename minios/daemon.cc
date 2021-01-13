// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "minios/daemon.h"

#include <dbus/minios/dbus-constants.h>
#include <sysexits.h>

namespace minios {

Daemon::Daemon() : DBusServiceDaemon(kMiniOsServiceName) {}

int Daemon::OnInit() {
  int return_code = brillo::DBusServiceDaemon::OnInit();
  if (return_code != EX_OK)
    return return_code;

  if (minios_.Run()) {
    LOG(ERROR) << "MiniOS failed to start.";
    return EX_SOFTWARE;
  }

  return EX_OK;
}

}  // namespace minios
