// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/socket_daemon.h"

#include <memory>
#include <utility>

#include <base/functional/bind.h>
#include <brillo/daemons/dbus_daemon.h>
#include <brillo/dbus/async_event_sequencer.h>
#include <chromeos/dbus/patchpanel/dbus-constants.h>

#include "patchpanel/minijailed_process_runner.h"
#include "patchpanel/socket_service_adaptor.h"

namespace patchpanel {

SocketDaemon::SocketDaemon(base::ScopedFD control_fd)
    : DBusServiceDaemon(kSocketServiceName),
      msg_dispatcher_(std::move(control_fd)) {
  msg_dispatcher_.RegisterFailureHandler(base::BindRepeating(
      &SocketDaemon::OnParentProcessExit, weak_factory_.GetWeakPtr()));
}

int SocketDaemon::OnInit() {
  EnterChildProcessJailWithNetAdmin();
  return DBusServiceDaemon::OnInit();
}

void SocketDaemon::OnShutdown(int* exit_code) {
  adaptor_.reset();

  if (bus_) {
    bus_->ShutdownAndBlock();
  }
  DBusServiceDaemon::OnShutdown(exit_code);
}

void SocketDaemon::RegisterDBusObjectsAsync(
    brillo::dbus_utils::AsyncEventSequencer* sequencer) {
  adaptor_ = std::make_unique<SocketServiceAdaptor>(bus_);
  adaptor_->RegisterAsync(
      sequencer->GetHandler("RegisterAsync() failed", true));
}

void SocketDaemon::OnParentProcessExit() {
  LOG(ERROR) << "Quitting because the parent process died";
  Quit();
}

}  // namespace patchpanel
