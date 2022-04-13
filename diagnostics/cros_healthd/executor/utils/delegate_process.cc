// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/executor/utils/delegate_process.h"

#include <string>
#include <utility>
#include <vector>

#include <mojo/public/cpp/bindings/remote.h>
#include <mojo/public/cpp/bindings/pending_remote.h>
#include <mojo/public/cpp/system/invitation.h>
#include <mojo/public/cpp/system/message_pipe.h>

#include "diagnostics/cros_healthd/executor/delegate_constants.h"
#include "diagnostics/cros_healthd/executor/mojom/delegate.mojom.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

constexpr char kDelegateBinary[] = "/usr/libexec/diagnostics/executor-delegate";

}  // namespace

DelegateProcess::DelegateProcess() = default;

DelegateProcess::~DelegateProcess() = default;

DelegateProcess::DelegateProcess(
    const std::string& seccomp_filename,
    const std::string& user,
    uint64_t capabilities_mask,
    const std::vector<base::FilePath>& readonly_mount_points,
    const std::vector<base::FilePath>& writable_mount_points)
    : SandboxedProcess({kDelegateBinary},
                       seccomp_filename,
                       user,
                       capabilities_mask,
                       readonly_mount_points,
                       writable_mount_points) {
  SendMojoInvitation();
  RunDelegate();
}

DelegateProcess::DelegateProcess(
    const std::string& seccomp_filename,
    const std::vector<base::FilePath>& readonly_mount_points)
    : SandboxedProcess(
          {kDelegateBinary}, seccomp_filename, readonly_mount_points) {
  SendMojoInvitation();
  RunDelegate();
}

void DelegateProcess::SendMojoInvitation() {
  mojo::OutgoingInvitation invitation;
  mojo::ScopedMessagePipeHandle pipe = invitation.AttachMessagePipe(0);
  mojo::OutgoingInvitation::Send(std::move(invitation),
                                 base::kNullProcessHandle,
                                 channel_.TakeLocalEndpoint());
  remote_.Bind(mojo::PendingRemote<mojom::Delegate>(std::move(pipe), 0));
}

void DelegateProcess::RunDelegate() {
  base::LaunchOptions options;
  std::string value;
  channel_.PrepareToPassRemoteEndpoint(&options.fds_to_remap, &value);

  AddArg(std::string("--") + kDelegateMojoChannelHandle + "=" + value);

  for (const auto& pii : options.fds_to_remap) {
    BindFd(pii.first, pii.second);
  }

  SandboxedProcess::Start();
  channel_.RemoteProcessLaunchAttempted();
}

}  // namespace diagnostics
