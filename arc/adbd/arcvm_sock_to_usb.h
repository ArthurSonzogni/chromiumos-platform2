/*
 * Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ARC_ADBD_ARCVM_SOCK_TO_USB_H_
#define ARC_ADBD_ARCVM_SOCK_TO_USB_H_

#include "arc/adbd/fd_splice.h"

namespace adbd {

// Reference:
// https://android.googlesource.com/platform/system/core/+/HEAD/adb/adb.h
// We take the bigger value of macro MAX_PAYLOAD from AOSP ADB code for the size
// of the buffer to USB.
constexpr size_t kAdbPayloadMaxSize = 1024 * 1024;
// Reference:
// https://android.googlesource.com/platform/system/core/+/HEAD/adb/types.h
// The offset is derived from the data_length field in struct amessage in
// types.h.
constexpr uint8_t kAmessageDataLenOffset = 12;
// Also from the types.h, the total length of an amessage instance is:
constexpr uint8_t kAmessageSize = 24;
// Size of the buffer to write to USB (IN) endpoint.
constexpr size_t kUsbWriteBufSize =
    kAdbPayloadMaxSize > kAmessageSize ? kAdbPayloadMaxSize : kAmessageSize;

// Provides a unidirectional channel to transfer
// ADB data from a socket to a USB endpoint.
class ArcVmSockToUsb : public FdSpliceThreadBase {
 public:
  ArcVmSockToUsb(const int sock_fd, const int usb_fd, const int stop_fd = -1);

  // Disallows copy and assignment.
  ArcVmSockToUsb(const ArcVmSockToUsb&) = delete;
  ArcVmSockToUsb& operator=(const ArcVmSockToUsb&) = delete;

  ~ArcVmSockToUsb();

 protected:
  void Run() override;
};

}  // namespace adbd

#endif  // ARC_ADBD_ARCVM_SOCK_TO_USB_H_
