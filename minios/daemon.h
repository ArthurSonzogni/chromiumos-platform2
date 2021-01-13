// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MINIOS_DAEMON_H_
#define MINIOS_DAEMON_H_

#include <memory>

#include <brillo/daemons/dbus_daemon.h>

#include "minios/minios.h"

namespace minios {

// |Daemon| is a D-Bus service daemon.
class Daemon : public brillo::DBusServiceDaemon {
 public:
  Daemon();
  ~Daemon() override = default;

  Daemon(const Daemon&) = delete;
  Daemon& operator=(const Daemon&) = delete;

 private:
  // |brillo::Daemon| overrides:
  int OnInit() override;

  MiniOs minios_;
};

}  // namespace minios

#endif  // MINIOS_DAEMON_H__
