// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo_service_manager/daemon/daemon.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <sysexits.h>

#include <memory>
#include <utility>

#include <base/bind.h>
#include <base/check_op.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/notreached.h>
#include <base/posix/eintr_wrapper.h>
#include <base/threading/thread_task_runner_handle.h>

namespace chromeos {
namespace mojo_service_manager {
namespace {

base::ScopedFD CreateUnixDomainSocket(const base::FilePath& socket_path) {
  base::ScopedFD socket_fd{
      socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0)};
  if (!socket_fd.is_valid()) {
    PLOG(ERROR) << "Failed to create socket.";
    return base::ScopedFD{};
  }

  struct sockaddr_un unix_addr {
    .sun_family = AF_UNIX,
  };
  constexpr size_t kMaxSize =
      sizeof(unix_addr.sun_path) - /*NULL-terminator*/ 1;
  CHECK_LE(socket_path.value().size(), kMaxSize);
  strncpy(unix_addr.sun_path, socket_path.value().c_str(), kMaxSize);

  if (bind(socket_fd.get(), reinterpret_cast<const sockaddr*>(&unix_addr),
           sizeof(unix_addr)) < 0) {
    PLOG(ERROR) << "Failed to bind: " << socket_path.value();
    return base::ScopedFD{};
  }

  if (listen(socket_fd.get(), SOMAXCONN) < 0) {
    PLOG(ERROR) << "Failed to listen " << socket_path.value();
    return base::ScopedFD{};
  }

  return socket_fd;
}

base::ScopedFD AcceptSocket(const base::ScopedFD& server_fd) {
  return base::ScopedFD{HANDLE_EINTR(accept4(server_fd.get(), nullptr, nullptr,
                                             SOCK_NONBLOCK | SOCK_CLOEXEC))};
}

}  // namespace

Daemon::Daemon(const base::FilePath& socket_path,
               Configuration configuration,
               ServicePolicyMap policy_map)
    : ipc_support_(base::ThreadTaskRunnerHandle::Get(),
                   mojo::core::ScopedIPCSupport::ShutdownPolicy::
                       CLEAN /* blocking shutdown */),
      socket_path_(socket_path),
      service_manager_(std::move(configuration), std::move(policy_map)) {}

Daemon::~Daemon() {}

int Daemon::OnInit() {
  int ret = brillo::Daemon::OnInit();
  if (ret != EX_OK)
    return ret;

  socket_fd_ = CreateUnixDomainSocket(socket_path_);
  if (!socket_fd_.is_valid()) {
    LOG(ERROR) << "Failed to create socket server at path: " << socket_path_;
    return EX_OSERR;
  }
  socket_watcher_ = base::FileDescriptorWatcher::WatchReadable(
      socket_fd_.get(),
      base::BindRepeating(&Daemon::SendMojoInvitationAndBindReceiver,
                          base::Unretained(this)));

  LOG(INFO) << "mojo_service_manager started.";
  return EX_OK;
}

void Daemon::OnShutdown(int* exit_code) {
  LOG(INFO) << "mojo_service_manager is shutdowning with exit code: "
            << *exit_code;
}

void Daemon::SendMojoInvitationAndBindReceiver() {
  base::ScopedFD peer = AcceptSocket(socket_fd_);
  if (!peer.is_valid()) {
    LOG(ERROR) << "Failed to accept peer socket";
    return;
  }
  NOTIMPLEMENTED();
}

}  // namespace mojo_service_manager
}  // namespace chromeos
