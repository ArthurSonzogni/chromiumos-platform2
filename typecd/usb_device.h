// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TYPECD_USB_DEVICE_H_
#define TYPECD_USB_DEVICE_H_

#include <string>

namespace typecd {

// This class is used to represent a USB device. It maintains Type C port that
// the USB device is connected to.
class UsbDevice {
 public:
  UsbDevice(int busnum, int devnum, std::string hub);

  void SetTypecPortNum(int typec_port_num) { typec_port_num_ = typec_port_num; }

  int GetBusnum() { return busnum_; }
  int GetDevnum() { return devnum_; }
  int GetTypecPortNum() { return typec_port_num_; }

 private:
  int busnum_;
  int devnum_;
  int typec_port_num_;

  // Root hub number and hub port number in accordance with the USB device sysfs
  // directory name. (e.g. 2-1 if sysfs path is /sys/bus/usb/devices/2-1)
  std::string hub_;
};

}  // namespace typecd

#endif  // TYPECD_USB_DEVICE_H_
