// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo_service_manager/daemon/daemon_test_helper.h"

#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <cstdlib>
#include <string>
#include <utility>

#include <base/check_op.h>
#include <base/files/file_util.h>
#include <base/posix/eintr_wrapper.h>
#include <base/test/bind.h>
#include <base/test/test_timeouts.h>
#include <base/threading/platform_thread.h>
#include <base/timer/elapsed_timer.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/public/cpp/bindings/remote.h>
#include <mojo/public/cpp/platform/platform_channel.h>
#include <mojo/public/cpp/system/invitation.h>

#include "mojo_service_manager/daemon/daemon.h"
#include "mojo_service_manager/daemon/mojo_test_environment.h"

namespace {

namespace mojo_service_manager = chromeos::mojo_service_manager;
namespace mojom = mojo_service_manager::mojom;

bool WaitForSocketReady(const base::FilePath& socket_path) {
  for (base::ElapsedTimer t; t.Elapsed() < TestTimeouts::action_timeout();
       base::PlatformThread::Sleep(base::Milliseconds(1))) {
    if (base::PathExists(socket_path))
      return true;
  }
  return false;
}

base::ScopedFD ConnectToSocket(const base::FilePath& socket_path) {
  if (!WaitForSocketReady(socket_path))
    return base::ScopedFD{};

  base::ScopedFD sock{socket(AF_UNIX, SOCK_STREAM, 0)};
  if (!sock.is_valid()) {
    LOG(ERROR) << "Failed to create socket";
    return base::ScopedFD{};
  }

  struct sockaddr_un unix_addr {
    .sun_family = AF_UNIX,
  };
  constexpr size_t kMaxSize =
      sizeof(unix_addr.sun_path) - /*NULL-terminator*/ 1;
  CHECK_LE(socket_path.value().size(), kMaxSize);
  strncpy(unix_addr.sun_path, socket_path.value().c_str(), kMaxSize);

  int rc = HANDLE_EINTR(connect(sock.get(),
                                reinterpret_cast<const sockaddr*>(&unix_addr),
                                sizeof(unix_addr)));
  if (rc == -1 && errno != EISCONN) {
    LOG(ERROR) << "Failed to connect to socket";
    return base::ScopedFD{};
  }
  return sock;
}

mojo::Remote<mojom::ServiceManager> ConnectToMojoServiceManager(
    base::ScopedFD peer) {
  CHECK(peer.is_valid());
  auto invitation = mojo::IncomingInvitation::Accept(
      mojo::PlatformChannelEndpoint(mojo::PlatformHandle(std::move(peer))));
  mojo::ScopedMessagePipeHandle pipe = invitation.ExtractMessagePipe(
      mojo_service_manager::kMojoInvitationPipeName);
  return mojo::Remote<mojom::ServiceManager>{
      mojo::PendingRemote<mojom::ServiceManager>(std::move(pipe), 0u)};
}

}  // namespace

int main(int argc, char** argv) {
  base::CommandLine::Init(argc, argv);
  TestTimeouts::Initialize();
  mojo::core::Init();
  mojo_service_manager::MojoTaskEnvironment env;

  base::FilePath socket_path =
      base::CommandLine::ForCurrentProcess()->GetSwitchValuePath(
          mojo_service_manager::kSocketPathSwitch);
  mojo::Remote<mojom::ServiceManager> mojo_service_manager =
      ConnectToMojoServiceManager(ConnectToSocket(socket_path));
  CHECK(mojo_service_manager.is_connected());

  mojo_service_manager::DaemonTestHelperResult result =
      mojo_service_manager::DaemonTestHelperResult::kConnectSuccessfully;
  mojo_service_manager.set_disconnect_with_reason_handler(
      base::BindLambdaForTesting([&](uint32_t error,
                                     const std::string& message) {
        CHECK_EQ(error,
                 static_cast<uint32_t>(mojom::ErrorCode::kUnexpectedOsError));
        result =
            mojo_service_manager::DaemonTestHelperResult::kResetWithOsError;
      }));

  // Make sure the state is updated.
  mojo_service_manager.FlushForTesting();
  return static_cast<int>(result);
}
