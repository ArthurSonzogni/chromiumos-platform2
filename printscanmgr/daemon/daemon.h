// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PRINTSCANMGR_DAEMON_DAEMON_H_
#define PRINTSCANMGR_DAEMON_DAEMON_H_

#include <brillo/daemons/dbus_daemon.h>

namespace printscanmgr {

class Daemon final : public brillo::DBusServiceDaemon {
 public:
  Daemon();
  Daemon(const Daemon&) = delete;
  Daemon& operator=(const Daemon&) = delete;
  ~Daemon() override;
};

}  // namespace printscanmgr

#endif  // PRINTSCANMGR_DAEMON_DAEMON_H_
