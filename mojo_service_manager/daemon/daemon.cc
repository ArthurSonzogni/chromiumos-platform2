// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo_service_manager/daemon/daemon.h"

#include <sysexits.h>

#include <memory>
#include <utility>

namespace chromeos {
namespace mojo_service_manager {

Daemon::Daemon() : mojo_thread_("mojo thread") {
  mojo_thread_.StartWithOptions(
      base::Thread::Options(base::MessagePumpType::IO, 0));
  mojo_task_runner_ = mojo_thread_.task_runner();
  ipc_support_ = std::make_unique<mojo::core::ScopedIPCSupport>(
      mojo_task_runner_, mojo::core::ScopedIPCSupport::ShutdownPolicy::
                             CLEAN /* blocking shutdown */);
}

Daemon::~Daemon() {}

int Daemon::OnEventLoopStarted() {
  // TODO(chungsheng): Add implementation.
  return EX_OK;
}

}  // namespace mojo_service_manager
}  // namespace chromeos
