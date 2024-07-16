// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/executor/executor_daemon.h"

#include <memory>
#include <utility>

#include <base/check.h>
#include <base/task/single_thread_task_runner.h>
#include <mojo/public/cpp/bindings/pending_receiver.h>
#include <mojo/public/cpp/system/invitation.h>
#include <mojo/public/cpp/system/message_pipe.h>

#include "diagnostics/cros_healthd/mojom/executor.mojom.h"
#include "diagnostics/cros_healthd/service_config.h"

namespace diagnostics {

namespace mojom = ::ash::cros_healthd::mojom;

ExecutorDaemon::ExecutorDaemon(mojo::PlatformChannelEndpoint endpoint,
                               const ServiceConfig& service_config)
    : mojo_task_runner_(base::SingleThreadTaskRunner::GetCurrentDefault()) {
  DCHECK(endpoint.is_valid());

  ipc_support_ = std::make_unique<mojo::core::ScopedIPCSupport>(
      mojo_task_runner_, mojo::core::ScopedIPCSupport::ShutdownPolicy::
                             CLEAN /* blocking shutdown */);

  // This accepts invitation from cros_healthd. Must be the incoming invitation
  // because cros_healthd is the process which connects to the mojo broker. This
  // must be run after the mojo ipc thread is initialized.
#if defined(ENABLE_IPCZ_ON_CHROMEOS)
  // IPCz requires an application to explicitly opt in to broker sharing
  // and inheritance when establishing a direct connection between two
  // non-broker nodes.
  mojo::IncomingInvitation invitation = mojo::IncomingInvitation::Accept(
      std::move(endpoint), MOJO_ACCEPT_INVITATION_FLAG_INHERIT_BROKER);
#else
  mojo::IncomingInvitation invitation =
      mojo::IncomingInvitation::Accept(std::move(endpoint));
#endif
  // Always use 0 as the default pipe name.
  mojo::ScopedMessagePipeHandle pipe = invitation.ExtractMessagePipe(0);

  process_reaper_.Register(this);

  mojo_service_ = std::make_unique<Executor>(
      mojo_task_runner_,
      mojo::PendingReceiver<mojom::Executor>(std::move(pipe)), &process_reaper_,
      base::BindOnce(&ExecutorDaemon::Quit, base::Unretained(this)),
      service_config);
}

ExecutorDaemon::~ExecutorDaemon() = default;

}  // namespace diagnostics
