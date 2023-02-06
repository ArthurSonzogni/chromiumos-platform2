// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "printscanmgr/daemon/daemon.h"

#include <dbus/printscanmgr/dbus-constants.h>

namespace printscanmgr {

Daemon::Daemon() : DBusServiceDaemon(kPrintscanmgrServiceName) {}
Daemon::~Daemon() = default;

}  // namespace printscanmgr
