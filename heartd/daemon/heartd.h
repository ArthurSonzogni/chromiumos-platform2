// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HEARTD_DAEMON_HEARTD_H_
#define HEARTD_DAEMON_HEARTD_H_

#include <memory>

#include <brillo/daemons/dbus_daemon.h>
#include <brillo/dbus/async_event_sequencer.h>
#include <mojo/core/embedder/scoped_ipc_support.h>

#include "heartd/daemon/action_runner.h"
#include "heartd/daemon/context.h"
#include "heartd/daemon/database.h"
#include "heartd/daemon/dbus_connector.h"
#include "heartd/daemon/heartbeat_manager.h"
#include "heartd/daemon/mojo_service.h"
#include "heartd/daemon/scavenger.h"
#include "heartd/daemon/top_sheriff.h"
#include "heartd/mojom/heartd.mojom.h"

namespace heartd {

class HeartdDaemon final : public brillo::DBusServiceDaemon {
 public:
  explicit HeartdDaemon(int sysrq_fd);
  HeartdDaemon(const HeartdDaemon&) = delete;
  HeartdDaemon& operator=(const HeartdDaemon&) = delete;
  ~HeartdDaemon() override;

 protected:
  // brillo::DBusServiceDaemon overrides:
  int OnEventLoopStarted() override;

 private:
  void ScavengerDelayTask();

 private:
  friend class HeartdDaemonTest;

 private:
  // For mojo thread initialization.
  std::unique_ptr<mojo::core::ScopedIPCSupport> ipc_support_;
  // Provides access to objects.
  std::unique_ptr<Context> context_ = nullptr;
  // Used to connect to dbus.
  std::unique_ptr<DbusConnector> dbus_connector_ = nullptr;
  // Used to run action.
  std::unique_ptr<ActionRunner> action_runner_ = nullptr;
  // Used to manage heartbeat service.
  std::unique_ptr<HeartbeatManager> heartbeat_manager_ = nullptr;
  // Used to provide mojo interface to mojo service manager.
  std::unique_ptr<HeartdMojoService> mojo_service_ = nullptr;
  // Used to run cleanup task.
  std::unique_ptr<Scavenger> scavenger_ = nullptr;
  // Used to manage sheriffs.
  std::unique_ptr<TopSheriff> top_sheriff_ = nullptr;
  // /proc/sysrq-trigger fd, this will be used in ActionRunner.
  int sysrq_fd_ = -1;
};

}  // namespace heartd

#endif  // HEARTD_DAEMON_HEARTD_H_
