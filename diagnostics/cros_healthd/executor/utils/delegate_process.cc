// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/executor/utils/delegate_process.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <base/functional/bind.h>
#include <base/task/sequenced_task_runner.h>
#include <mojo/public/cpp/bindings/pending_remote.h>
#include <mojo/public/cpp/bindings/remote.h>
#include <mojo/public/cpp/system/message_pipe.h>

#include "diagnostics/cros_healthd/delegate/constants.h"
#include "diagnostics/cros_healthd/mojom/delegate.mojom.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

constexpr char kDelegateBinary[] = "/usr/libexec/diagnostics/executor-delegate";

}  // namespace

DelegateProcess::DelegateProcess() = default;

DelegateProcess::~DelegateProcess() = default;

DelegateProcess::DelegateProcess(std::string_view seccomp_filename,
                                 const SandboxedProcess::Options& options)
    : SandboxedProcess({kDelegateBinary}, seccomp_filename, options) {
  mojo::ScopedMessagePipeHandle pipe = invitation_.AttachMessagePipe(0);
#if defined(USE_IPCZ)
  // IPCz requires an application to explicitly opt in to broker sharing
  // and inheritance when establishing a direct connection between two
  // non-broker nodes.
  invitation_.set_extra_flags(MOJO_SEND_INVITATION_FLAG_SHARE_BROKER);
#endif
  remote_.Bind(mojo::PendingRemote<mojom::Delegate>(std::move(pipe), 0));
}

void DelegateProcess::StartAsync() {
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(base::IgnoreResult(&DelegateProcess::Start),
                                weak_factory_.GetWeakPtr()));
}

bool DelegateProcess::Start() {
  mojo::PlatformChannel channel;
  mojo::OutgoingInvitation::Send(std::move(invitation_),
                                 base::kNullProcessHandle,
                                 channel.TakeLocalEndpoint());
  base::LaunchOptions options;
  std::string value;
  channel.PrepareToPassRemoteEndpoint(&options.fds_to_remap, &value);

  AddArg(std::string("--") + kDelegateMojoChannelHandle + "=" + value);

  for (const auto& [parent_fd, child_fd] : options.fds_to_remap) {
    BindFd(parent_fd, child_fd);
  }

  bool res = SandboxedProcess::Start();
  channel.RemoteProcessLaunchAttempted();
  return res;
}

}  // namespace diagnostics
