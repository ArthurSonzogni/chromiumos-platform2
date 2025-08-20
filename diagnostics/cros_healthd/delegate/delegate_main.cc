// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/command_line.h>
#include <base/task/single_thread_task_runner.h>
#include <brillo/daemons/daemon.h>
#include <brillo/syslog_logging.h>
#include <libec/ec_command_factory.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/core/embedder/scoped_ipc_support.h>
#include <mojo/public/cpp/bindings/pending_receiver.h>
#include <mojo/public/cpp/bindings/receiver.h>
#include <mojo/public/cpp/platform/platform_channel.h>
#include <mojo/public/cpp/system/invitation.h>
#include <mojo/public/cpp/system/message_pipe.h>

#include "diagnostics/cros_healthd/delegate/constants.h"
#include "diagnostics/cros_healthd/delegate/delegate_impl.h"
#include "diagnostics/cros_healthd/delegate/utils/display_util_factory_impl.h"

namespace diagnostics {

namespace mojom = ::ash::cros_healthd::mojom;

class DelegateDaemon : public brillo::Daemon {
 public:
  explicit DelegateDaemon(mojo::PlatformChannelEndpoint endpoint)
      : scoped_ipc_support_(base::SingleThreadTaskRunner::
                                GetCurrentDefault() /* io_thread_task_runner */,
                            mojo::core::ScopedIPCSupport::ShutdownPolicy::
                                CLEAN /* blocking shutdown */) {
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
    mojo::ScopedMessagePipeHandle pipe = invitation.ExtractMessagePipe(0);
    receiver_.Bind(mojo::PendingReceiver<mojom::Delegate>(std::move(pipe)));
  }
  DelegateDaemon(const DelegateDaemon&) = delete;
  DelegateDaemon& operator=(const DelegateDaemon&) = delete;
  ~DelegateDaemon();

 private:
  mojo::core::ScopedIPCSupport scoped_ipc_support_;
  ec::EcCommandFactory ec_command_factory_;
  DisplayUtilFactoryImpl display_util_factory_;
  ec::EcCommandVersionSupported ec_command_version_supported_;
  DelegateImpl delegate_{&ec_command_factory_, &display_util_factory_,
                         &ec_command_version_supported_};
  mojo::Receiver<mojom::Delegate> receiver_{&delegate_};
};

DelegateDaemon::~DelegateDaemon() {}

}  // namespace diagnostics

int main(int argc, char* argv[]) {
  brillo::InitLog(brillo::kLogToSyslog);
  base::CommandLine::Init(argc, argv);

  DLOG(INFO) << "Start cros_healthd executer delegate.";

  mojo::core::Init();

  diagnostics::DelegateDaemon daemon(
      mojo::PlatformChannel::RecoverPassedEndpointFromString(
          base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
              diagnostics::kDelegateMojoChannelHandle)));
  return daemon.Run();
}
