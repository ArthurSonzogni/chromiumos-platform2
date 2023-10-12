/*
 * Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ARC_ADBD_ARCVM_USB_TO_SOCK_H_
#define ARC_ADBD_ARCVM_USB_TO_SOCK_H_

#include "arc/adbd/fd_splice.h"

namespace adbd {

// Provides a unidirectional channel to transfer.
// ADB data from a USB endpoint to a socket.
class ArcVmUsbToSock : public FdSpliceThreadBase {
 public:
  ArcVmUsbToSock(const int sock_fd, const int usb_fd, const int stop_fd = -1);

  // Disallows copy and assignment.
  ArcVmUsbToSock(const ArcVmUsbToSock&) = delete;
  ArcVmUsbToSock& operator=(const ArcVmUsbToSock&) = delete;

  ~ArcVmUsbToSock();

 protected:
  void Run() override;
};

}  // namespace adbd

#endif  // ARC_ADBD_ARCVM_USB_TO_SOCK_H_
