// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "heartd/daemon/heartd.h"

#include <memory>
#include <sysexits.h>

#include <base/files/file_path.h>
#include <base/task/single_thread_task_runner.h>
#include <base/time/time.h>
#include <dbus/heartd/dbus-constants.h>

#include "heartd/daemon/dbus_connector_impl.h"
#include "heartd/daemon/sheriffs/boot_metrics_recorder.h"
#include "heartd/daemon/sheriffs/sheriff.h"

namespace heartd {

using brillo::dbus_utils::AsyncEventSequencer;

HeartdDaemon::HeartdDaemon(int sysrq_fd)
    : brillo::DBusServiceDaemon(kHeartdServiceName), sysrq_fd_(sysrq_fd) {
  ipc_support_ = std::make_unique<mojo::core::ScopedIPCSupport>(
      base::SingleThreadTaskRunner::GetCurrentDefault(),
      mojo::core::ScopedIPCSupport::ShutdownPolicy::CLEAN);
}

HeartdDaemon::~HeartdDaemon() = default;

int HeartdDaemon::OnEventLoopStarted() {
  const int exit_code = DBusServiceDaemon::OnEventLoopStarted();
  if (exit_code != EX_OK) {
    return exit_code;
  }
  if (sysrq_fd_ == -1) {
    return EX_UNAVAILABLE;
  }

  database_ = std::make_unique<Database>();
  database_->Init();
  dbus_connector_ = std::make_unique<DbusConnectorImpl>();
  action_runner_ = std::make_unique<ActionRunner>(dbus_connector_.get());
  heartbeat_manager_ = std::make_unique<HeartbeatManager>(action_runner_.get());
  mojo_service_ = std::make_unique<HeartdMojoService>(heartbeat_manager_.get(),
                                                      action_runner_.get());

  top_sheriff_ = std::make_unique<TopSheriff>(
      base::BindOnce(&Daemon::Quit, base::Unretained(this)),
      heartbeat_manager_.get());
  top_sheriff_->AddSheriff(std::unique_ptr<Sheriff>(
      new BootMetricsRecorder(base::FilePath("/"), database_.get())));
  top_sheriff_->GetToWork();

  action_runner_->SetupSysrq(sysrq_fd_);

  // We have to cache the boot record when start up, because when we need to
  // trigger the reboot action, it's possible that we can't read the database
  // successfully.
  action_runner_->CacheBootRecord(
      database_->GetBootRecordFromTime(base::Time().Now() - base::Days(7)));

  return EX_OK;
}

}  // namespace heartd
