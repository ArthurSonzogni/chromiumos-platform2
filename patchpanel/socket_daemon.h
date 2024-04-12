// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_SOCKET_DAEMON_H_
#define PATCHPANEL_SOCKET_DAEMON_H_

#include <memory>

#include <brillo/daemons/dbus_daemon.h>
#include <brillo/dbus/async_event_sequencer.h>
#include <base/memory/weak_ptr.h>
#include <base/files/scoped_file.h>

#include "patchpanel/ipc.h"
#include "patchpanel/message_dispatcher.h"
#include "patchpanel/socket_service_adaptor.h"

namespace patchpanel {

// SocketDaemon hosts a D-Bus service dedicated to handle socket tag requests.
// It allows to tag socket synchronously without blocking patchpanel main D-Bus
// API.
class SocketDaemon : public brillo::DBusServiceDaemon {
 public:
  explicit SocketDaemon(base::ScopedFD control_fd);
  SocketDaemon(const SocketDaemon&) = delete;
  SocketDaemon& operator=(const SocketDaemon&) = delete;

 protected:
  // Override of DBusServiceDaemon methods.
  int OnInit() override;
  void OnShutdown(int* exit_code) override;
  void RegisterDBusObjectsAsync(
      brillo::dbus_utils::AsyncEventSequencer* sequencer) override;

  // Callback to be notified when the parent process quits.
  void OnParentProcessExit();

 private:
  // Communication channel with the parent process.
  MessageDispatcher<SubprocessMessage> msg_dispatcher_;
  // DBus implementation of the SocketService.
  std::unique_ptr<SocketServiceAdaptor> adaptor_;

  base::WeakPtrFactory<SocketDaemon> weak_factory_{this};
};

}  // namespace patchpanel

#endif  // PATCHPANEL_SOCKET_DAEMON_H_
