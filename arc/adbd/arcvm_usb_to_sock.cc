/*
 * Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "arc/adbd/arcvm_usb_to_sock.h"

#include <fcntl.h>
#include <unistd.h>

#include <vector>

#include <base/check_op.h>
#include <base/functional/bind.h>
#include <base/logging.h>

namespace adbd {
namespace {

// Size of the buffer read from USB (OUT) endpoint.
constexpr size_t kUsbReadBufSize = 4 * 1024;
}  // namespace

// Setup FdSpliceThreadBase with in_fd_(usb_fd), out_fd_(sock_fd)
ArcVmUsbToSock::ArcVmUsbToSock(const int sock_fd,
                               const int usb_fd,
                               const int stop_fd)
    : FdSpliceThreadBase("ArcVmUsbToSock", usb_fd, sock_fd, stop_fd) {}

ArcVmUsbToSock::~ArcVmUsbToSock() = default;

void ArcVmUsbToSock::Run() {
  std::vector<char> buf(kUsbReadBufSize);

  // Most of the time we will be blocked in reading from USB
  // Process any data pending in the buffer first before pull
  // more from USB endpoint.
  while (true) {
    char* data = buf.data();
    // When any channel broke, there is no point to keep the whole bridge
    // up, so exit the thread in such cases.

    // Read from usb (fd_in)
    auto ret = ReadOnce(data, kUsbReadBufSize);
    if (ret < 0) {
      LOG(WARNING) << "ArcVmUsbToSock exiting";
      return;
    }
    // Write to sock (fd_out)
    if (!WriteAll(data, ret)) {
      LOG(WARNING) << "ArcVmUsbToSock exiting";
      return;
    }
  }
}

}  // namespace adbd
