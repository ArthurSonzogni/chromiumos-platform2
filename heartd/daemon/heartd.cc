// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "heartd/daemon/heartd.h"

#include <memory>
#include <sysexits.h>

#include <base/files/file_path.h>
#include <base/task/single_thread_task_runner.h>

#include "heartd/daemon/dbus_connector_impl.h"
#include "heartd/daemon/utils/boot_record_recorder.h"

namespace heartd {

HeartdDaemon::HeartdDaemon(int sysrq_fd) {
  ipc_support_ = std::make_unique<mojo::core::ScopedIPCSupport>(
      base::SingleThreadTaskRunner::GetCurrentDefault(),
      mojo::core::ScopedIPCSupport::ShutdownPolicy::CLEAN);
  database_ = std::make_unique<Database>();
  database_->Init();
  dbus_connector_ = std::make_unique<DbusConnectorImpl>();
  action_runner_ = std::make_unique<ActionRunner>(dbus_connector_.get());
  heartbeat_manager_ = std::make_unique<HeartbeatManager>(action_runner_.get());
  mojo_service_ = std::make_unique<HeartdMojoService>(heartbeat_manager_.get(),
                                                      action_runner_.get());

  action_runner_->SetupSysrq(sysrq_fd);
}

HeartdDaemon::~HeartdDaemon() = default;

int HeartdDaemon::OnEventLoopStarted() {
  RecordBootMetrics(base::FilePath("/"), database_.get());
  database_->RemoveOutdatedData(kBootRecordTable);

  return EX_OK;
}

}  // namespace heartd
