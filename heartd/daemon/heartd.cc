// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "heartd/daemon/heartd.h"

#include <memory>

#include <base/task/single_thread_task_runner.h>

namespace heartd {

HeartdDaemon::HeartdDaemon() {
  ipc_support_ = std::make_unique<mojo::core::ScopedIPCSupport>(
      base::SingleThreadTaskRunner::GetCurrentDefault(),
      mojo::core::ScopedIPCSupport::ShutdownPolicy::CLEAN);
  action_runner_ = std::make_unique<ActionRunner>();
  heartbeat_manager_ = std::make_unique<HeartbeatManager>();
  mojo_service_ = std::make_unique<HeartdMojoService>(heartbeat_manager_.get(),
                                                      action_runner_.get());
}

HeartdDaemon::~HeartdDaemon() = default;

}  // namespace heartd
