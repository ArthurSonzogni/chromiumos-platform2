// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/command_line.h>
#include <brillo/daemons/daemon.h>
#include <brillo/syslog_logging.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/core/embedder/scoped_ipc_support.h>
#include <mojo/public/cpp/bindings/pending_receiver.h>
#include <mojo/public/cpp/bindings/receiver.h>
#include <mojo/public/cpp/platform/platform_channel.h>
#include <mojo/public/cpp/system/invitation.h>
#include <mojo/public/cpp/system/message_pipe.h>

#include "diagnostics/cros_healthd/executor/delegate_constants.h"
#include "diagnostics/cros_healthd/executor/delegate_impl.h"

namespace diagnostics {

class DelegateDaemon : public brillo::Daemon {
 public:
  explicit DelegateDaemon(mojo::PlatformChannelEndpoint endpoint)
      : scoped_ipc_support_(
            base::ThreadTaskRunnerHandle::Get() /* io_thread_task_runner */,
            mojo::core::ScopedIPCSupport::ShutdownPolicy::
                CLEAN /* blocking shutdown */) {
    mojo::IncomingInvitation invitation =
        mojo::IncomingInvitation::Accept(std::move(endpoint));
    mojo::ScopedMessagePipeHandle pipe = invitation.ExtractMessagePipe(0);
    receiver_.Bind(mojo::PendingReceiver<mojom::Delegate>(std::move(pipe)));
    receiver_.set_disconnect_handler(
        base::BindOnce(&DelegateDaemon::Quit, base::Unretained(this)));
  }
  DelegateDaemon(const DelegateDaemon&) = delete;
  DelegateDaemon& operator=(const DelegateDaemon&) = delete;
  ~DelegateDaemon();

 private:
  mojo::core::ScopedIPCSupport scoped_ipc_support_;

  DelegateImpl delegate_;

  mojo::Receiver<mojom::Delegate> receiver_{&delegate_};
};

DelegateDaemon::~DelegateDaemon() {}

}  // namespace diagnostics

int main(int argc, char* argv[]) {
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderr);
  base::CommandLine::Init(argc, argv);

  mojo::core::Init();

  diagnostics::DelegateDaemon daemon(
      mojo::PlatformChannel::RecoverPassedEndpointFromString(
          base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
              diagnostics::kDelegateMojoChannelHandle)));
  return daemon.Run();
}
