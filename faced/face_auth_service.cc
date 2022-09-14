// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "faced/face_auth_service.h"

#include <memory>
#include <utility>

#include <absl/status/status.h>
#include <base/bind.h>
#include <base/logging.h>
#include <chromeos/dbus/service_constants.h>
#include <mojo/public/cpp/system/invitation.h>

#include "faced/mojom/face_auth.mojom.h"

namespace faced {

FaceAuthService::FaceAuthService() : ipc_thread_("FaceAuthIPC") {}

absl::StatusOr<std::unique_ptr<FaceAuthService>> FaceAuthService::Create() {
  // Create the result FaceAuthService object.
  //
  // Using `new` to access private constructor of `FaceAuthService`.
  std::unique_ptr<FaceAuthService> result(new FaceAuthService());

  if (!result->ipc_thread_.StartWithOptions(
          base::Thread::Options(base::MessagePumpType::IO, 0))) {
    return absl::InternalError("Failed to start IPC thread.");
  }

  result->ipc_support_ = std::make_unique<mojo::core::ScopedIPCSupport>(
      result->ipc_thread_.task_runner(),
      mojo::core::ScopedIPCSupport::ShutdownPolicy::FAST);

  result->mojo_task_runner_ = result->ipc_thread_.task_runner();

  return result;
}

void FaceAuthService::SetCriticalErrorCallback(
    CriticalErrorCallback error_callback,
    scoped_refptr<base::TaskRunner> task_runner) {
  DCHECK(task_runner);
  error_callback_ = std::move(error_callback);
  error_task_runner_ = std::move(task_runner);
}

void FaceAuthService::ReceiveMojoInvitation(
    base::ScopedFD fd,
    ReceiveOnIpcThreadCallback callback,
    scoped_refptr<base::TaskRunner> callback_runner) {
  mojo::IncomingInvitation invitation = mojo::IncomingInvitation::Accept(
      mojo::PlatformChannelEndpoint(mojo::PlatformHandle(std::move(fd))));

  mojo_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&FaceAuthService::SetupMojoPipeOnThread,
                                base::Unretained(this), std::move(invitation),
                                std::move(callback), callback_runner));
}

void FaceAuthService::SetupMojoPipeOnThread(
    mojo::IncomingInvitation invitation,
    ReceiveOnIpcThreadCallback callback,
    scoped_refptr<base::TaskRunner> callback_runner) {
  DCHECK(mojo_task_runner_->BelongsToCurrentThread());

  mojo::ScopedMessagePipeHandle mojo_pipe_handle =
      invitation.ExtractMessagePipe(kBootstrapMojoConnectionChannelToken);
  if (!mojo_pipe_handle.is_valid()) {
    callback_runner->PostTask(
        FROM_HERE, base::BindOnce(std::move(callback), /*success=*/false));
    return;
  }

  service_ = std::make_unique<FaceAuthServiceImpl>(
      mojo::PendingReceiver<
          chromeos::face_auth::mojom::FaceAuthenticationService>(
          std::move(mojo_pipe_handle)),
      base::BindOnce(&FaceAuthService::OnConnectionError,
                     base::Unretained(this)));

  callback_runner->PostTask(
      FROM_HERE, base::BindOnce(std::move(callback), /*success=*/true));

  LOG(INFO) << "Mojo connection bootstrapped.";
}

void FaceAuthService::OnConnectionError() {
  error_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(std::move(error_callback_),
                                "Lost mojo connection to primary broker"));
}

}  // namespace faced
