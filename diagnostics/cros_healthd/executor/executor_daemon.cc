// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/executor/executor_daemon.h"

#include <memory>
#include <utility>

#include <base/check.h>
#include <base/task/thread_pool/thread_pool_instance.h>
#include <base/threading/thread_task_runner_handle.h>
#include <mojo/public/cpp/bindings/pending_receiver.h>
#include <mojo/public/cpp/system/invitation.h>
#include <mojo/public/cpp/system/message_pipe.h>

#include "diagnostics/cros_healthd/executor/mojom/executor.mojom.h"

namespace diagnostics {

namespace mojom = ::ash::cros_healthd::mojom;

ExecutorDaemon::ExecutorDaemon(mojo::PlatformChannelEndpoint endpoint)
    : mojo_task_runner_(base::ThreadTaskRunnerHandle::Get()) {
  DCHECK(endpoint.is_valid());

  // We'll use the thread pool to run tasks that can be cancelled. Otherwise,
  // cancel requests will be queued and only run after the task finishes, which
  // defeats the purpose of the cancel request.
  base::ThreadPoolInstance::CreateAndStartWithDefaultParams(
      "cros_healthd executor");

  ipc_support_ = std::make_unique<mojo::core::ScopedIPCSupport>(
      mojo_task_runner_, mojo::core::ScopedIPCSupport::ShutdownPolicy::
                             CLEAN /* blocking shutdown */);

  // This accepts invitation from cros_healthd. Must be the incoming invitation
  // because cros_healthd is the process which connects to the mojo broker. This
  // must be run after the mojo ipc thread is initialized.
  mojo::IncomingInvitation invitation =
      mojo::IncomingInvitation::Accept(std::move(endpoint));
  // Always use 0 as the default pipe name.
  mojo::ScopedMessagePipeHandle pipe = invitation.ExtractMessagePipe(0);

  mojo_service_ = std::make_unique<Executor>(
      mojo_task_runner_,
      mojo::PendingReceiver<mojom::Executor>(std::move(pipe)),
      base::BindOnce(&ExecutorDaemon::Quit, base::Unretained(this)));
}

ExecutorDaemon::~ExecutorDaemon() = default;

}  // namespace diagnostics
