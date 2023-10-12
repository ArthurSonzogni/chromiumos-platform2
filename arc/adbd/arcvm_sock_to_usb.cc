/*
 * Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "arc/adbd/arcvm_sock_to_usb.h"

#include <vector>

#include <base/check_op.h>
#include <base/functional/bind.h>
#include <base/logging.h>

namespace adbd {

// Set up FdSpliceThreadBase with in_fd_(sock_fd), out_fd_(usb_fd)
ArcVmSockToUsb::ArcVmSockToUsb(const int sock_fd,
                               const int usb_fd,
                               const int stop_fd)
    : FdSpliceThreadBase("ArcVmSockToUsb", sock_fd, usb_fd, stop_fd) {}

ArcVmSockToUsb::~ArcVmSockToUsb() = default;

void ArcVmSockToUsb::Run() {
  std::vector<char> buf(kUsbWriteBufSize);

  while (true) {
    char* data = buf.data();
    // Read from sock (fd_in)
    if (!ReadAll(data, kAmessageSize)) {
      LOG(WARNING) << "error reading message header, ArcVmSockToUsb exiting";
      return;
    }

    // Write to usb (fd_out)
    if (!WriteAll(data, kAmessageSize)) {
      LOG(WARNING) << "error writing message header, ArcVmSockToUsb exiting";
      return;
    }

    // The ADB design of USB transport splits the header and the optional
    // data payload of a message into two USB transfers. The peer expects
    // the exact package length of each transfer to USB layers. But such
    // behavior seems not for socket transport. As a result, we have to
    // step into the traffic from the socket to split the data properly
    // before relaying the data to USB endpoint.
    // We achieve this by using the depth control of buffer. Data won't be
    // sent until we have the expected amount.
    size_t payload_len = 0;
    for (int i = 0; i < 4; i++) {
      payload_len +=
          static_cast<unsigned char>(data[kAmessageDataLenOffset + i]) << 8 * i;
    }
    if (payload_len > kAdbPayloadMaxSize) {
      LOG(ERROR) << "payload length is too big, ArcVmSockToUsb exiting";
      return;
    }
    if (payload_len > 0) {
      // Read from sock (fd_in)
      if (!ReadAll(data, payload_len)) {
        LOG(WARNING) << "error reading payload, ArcVmSockToUsb exiting";
        return;
      }
      // Write to usb (fd_out)
      if (!WriteAll(data, payload_len)) {
        LOG(WARNING) << "error writing payload, ArcVmSockToUsb exiting";
        return;
      }
    }
  }
}

}  // namespace adbd
