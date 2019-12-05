// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/network/adb_proxy.h"

#include <linux/vm_sockets.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sysexits.h>

#include <utility>

#include <base/bind.h>
#include <base/logging.h>
#include <base/strings/stringprintf.h>

#include "arc/network/minijailed_process_runner.h"
#include "arc/network/net_util.h"

namespace arc_networkd {
namespace {
// adb gets confused if we listen on 5555 and thinks there is an emulator
// running, which in turn ends up confusing our integration test libraries
// because multiple devices show up.
constexpr uint16_t kTcpListenPort = 5550;
// But we still connect to adbd on its standard TCP port.
constexpr uint16_t kTcpConnectPort = 5555;
constexpr uint32_t kTcpAddr = Ipv4Addr(100, 115, 92, 2);
constexpr uint32_t kVsockPort = 5555;
constexpr int kMaxConn = 16;

}  // namespace

AdbProxy::AdbProxy(base::ScopedFD control_fd)
    : msg_dispatcher_(std::move(control_fd)),
      arc_type_(GuestMessage::UNKNOWN_GUEST),
      arcvm_vsock_cid_(-1) {
  msg_dispatcher_.RegisterFailureHandler(
      base::Bind(&AdbProxy::OnParentProcessExit, weak_factory_.GetWeakPtr()));

  msg_dispatcher_.RegisterGuestMessageHandler(
      base::Bind(&AdbProxy::OnGuestMessage, weak_factory_.GetWeakPtr()));
}

AdbProxy::~AdbProxy() = default;

int AdbProxy::OnInit() {
  // Prevent the main process from sending us any signals.
  if (setsid() < 0) {
    PLOG(ERROR) << "Failed to created a new session with setsid; exiting";
    return EX_OSERR;
  }

  EnterChildProcessJail();
  return Daemon::OnInit();
}

void AdbProxy::Reset() {
  src_watcher_.reset();
  src_.reset();
  fwd_.clear();
  arcvm_vsock_cid_ = -1;
  arc_type_ = GuestMessage::UNKNOWN_GUEST;
}

void AdbProxy::OnParentProcessExit() {
  LOG(ERROR) << "Quitting because the parent process died";
  Reset();
  Quit();
}

void AdbProxy::OnFileCanReadWithoutBlocking() {
  if (auto conn = src_->Accept()) {
    if (auto dst = Connect()) {
      LOG(INFO) << "Connection established: " << *conn << " <-> " << *dst;
      auto fwd = std::make_unique<SocketForwarder>(
          base::StringPrintf("adbp%d-%d", conn->fd(), dst->fd()),
          std::move(conn), std::move(dst));
      fwd->Start();
      fwd_.emplace_back(std::move(fwd));
    }
  }

  // Cleanup any defunct forwarders.
  for (auto it = fwd_.begin(); it != fwd_.end();) {
    if (!(*it)->IsRunning() && (*it)->HasBeenStarted())
      it = fwd_.erase(it);
    else
      ++it;
  }
}

std::unique_ptr<Socket> AdbProxy::Connect() const {
  switch (arc_type_) {
    case GuestMessage::ARC:
    case GuestMessage::ARC_LEGACY: {
      struct sockaddr_in addr_in = {0};
      addr_in.sin_family = AF_INET;
      addr_in.sin_port = htons(kTcpConnectPort);
      addr_in.sin_addr.s_addr = kTcpAddr;
      auto dst = std::make_unique<Socket>(AF_INET, SOCK_STREAM);
      return dst->Connect((const struct sockaddr*)&addr_in, sizeof(addr_in))
                 ? std::move(dst)
                 : nullptr;
    }
    case GuestMessage::ARC_VM: {
      struct sockaddr_vm addr_vm = {0};
      addr_vm.svm_family = AF_VSOCK;
      addr_vm.svm_port = kVsockPort;
      addr_vm.svm_cid = arcvm_vsock_cid_;
      auto dst = std::make_unique<Socket>(AF_VSOCK, SOCK_STREAM);
      return dst->Connect((const struct sockaddr*)&addr_vm, sizeof(addr_vm))
                 ? std::move(dst)
                 : nullptr;
    }
    default:
      LOG(DFATAL) << "Unexpected connect - no ARC guest";
      return nullptr;
  }
}

void AdbProxy::OnGuestMessage(const GuestMessage& msg) {
  if (msg.type() == GuestMessage::UNKNOWN_GUEST) {
    LOG(DFATAL) << "Unexpected message from unknown guest";
    return;
  }

  arc_type_ = msg.type();
  arcvm_vsock_cid_ = msg.arcvm_vsock_cid();

  // On ARC up, start accepting connections.
  if (msg.event() == GuestMessage::START) {
    src_ = std::make_unique<Socket>(AF_INET, SOCK_STREAM | SOCK_NONBLOCK);
    // Need to set this to reuse the port on localhost.
    int on = 1;
    if (setsockopt(src_->fd(), SOL_SOCKET, SO_REUSEADDR, &on, sizeof(int)) <
        0) {
      PLOG(ERROR) << "setsockopt(SO_REUSEADDR) failed";
      return;
    }
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(kTcpListenPort);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (!src_->Bind((const struct sockaddr*)&addr, sizeof(addr))) {
      LOG(ERROR) << "Cannot bind source socket";
      return;
    }

    if (!src_->Listen(kMaxConn)) {
      LOG(ERROR) << "Cannot listen on source socket";
      return;
    }

    // Run the accept loop.
    LOG(INFO) << "Accepting connections...";
    src_watcher_ = base::FileDescriptorWatcher::WatchReadable(
        src_->fd(), base::BindRepeating(&AdbProxy::OnFileCanReadWithoutBlocking,
                                        base::Unretained(this)));
    return;
  }

  // On ARC down, cull any open connections and stop listening.
  if (msg.event() == GuestMessage::STOP) {
    Reset();
  }
}

}  // namespace arc_networkd
