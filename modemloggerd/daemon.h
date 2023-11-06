// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MODEMLOGGERD_DAEMON_H_
#define MODEMLOGGERD_DAEMON_H_

#include <memory>
#include <string>

#include <brillo/daemons/dbus_daemon.h>

#include "modemloggerd/adaptor_factory.h"
#include "modemloggerd/manager.h"

namespace modemloggerd {

class Daemon : public brillo::DBusServiceDaemon {
 public:
  Daemon();
  Daemon(const Daemon&) = delete;
  Daemon& operator=(const Daemon&) = delete;

 private:
  // brillo::Daemon override.
  void RegisterDBusObjectsAsync(
      brillo::dbus_utils::AsyncEventSequencer* sequencer) override;

  std::unique_ptr<Manager> manager_;
  std::unique_ptr<AdaptorFactoryInterface> adaptor_factory_;
};

}  // namespace modemloggerd

#endif  // MODEMLOGGERD_DAEMON_H_
