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
  void SetSpeed(int speed) { speed_ = speed; }
  void SetDeviceClass(int device_class) { device_class_ = device_class; }
  void SetInterfaceClass(int interface_class) {
    interface_class_ = interface_class;
  }

  int GetBusnum() { return busnum_; }
  int GetDevnum() { return devnum_; }
  int GetTypecPortNum() { return typec_port_num_; }
  int GetSpeed() { return speed_; }
  int GetDeviceClass() { return device_class_; }
  int GetInterfaceClass() { return interface_class_; }

 private:
  int busnum_;
  int devnum_;
  int typec_port_num_;

  // Root hub number and hub port number in accordance with the USB device sysfs
  // directory name. (e.g. 2-1 if sysfs path is /sys/bus/usb/devices/2-1)
  std::string hub_;

  // Determines USB standard that the device is using.
  // ===============================
  // Standard        | Speed (Mbps)
  // ===============================
  // USB 1.1         | 1.5 or 12
  // USB 2.0         | 480
  // USB 3.2 Gen 1   | 5000
  // USB 3.2 Gen 2   | 10000
  // USB 3.2 Gen 2x2 | 20000
  int speed_;

  // Identifies type of device.
  // https://www.usb.org/defined-class-codes
  int device_class_;
  int interface_class_;
};

}  // namespace typecd

#endif  // TYPECD_USB_DEVICE_H_
