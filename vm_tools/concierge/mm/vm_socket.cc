// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/mm/vm_socket.h"

#include <arpa/inet.h>
#include <sys/socket.h>

// Needs to be included after sys/socket.h.
#include <linux/vm_sockets.h>

#include <utility>

#include <base/logging.h>
#include <base/posix/eintr_wrapper.h>

namespace vm_tools::concierge::mm {

VmSocket::VmSocket() = default;

VmSocket::VmSocket(base::ScopedFD fd) : fd_(std::move(fd)) {}

bool VmSocket::Connect(int port, int timeout_ms) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!InitFd()) {
    return false;
  }

  // Set the read timeout
  struct timeval tv;
  tv.tv_sec = timeout_ms / 1000;
  timeout_ms -= (tv.tv_sec * 1000);
  tv.tv_usec = timeout_ms * 1000;
  if (setsockopt(fd_.get(), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) != 0) {
    PLOG(ERROR) << "Failed to set recv timeout on vsock.";
    fd_.reset();
    return false;
  }

  struct sockaddr_vm sa {};
  sa.svm_family = AF_VSOCK;
  sa.svm_port = port;
  sa.svm_cid = VMADDR_CID_LOCAL;

  if (HANDLE_EINTR(connect(fd_.get(),
                           reinterpret_cast<const struct sockaddr*>(&sa),
                           sizeof(sa))) == -1) {
    PLOG(ERROR) << "Failed to connect to vsock port " << port;
    fd_.reset();
    return false;
  }

  return true;
}

base::ScopedFD VmSocket::Release() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return std::move(fd_);
}

bool VmSocket::InitFd() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  fd_.reset(socket(AF_VSOCK, SOCK_STREAM, 0));

  if (!fd_.is_valid()) {
    PLOG(ERROR) << "Failed to create VSOCK.";
    return false;
  }

  return true;
}

}  // namespace vm_tools::concierge::mm
