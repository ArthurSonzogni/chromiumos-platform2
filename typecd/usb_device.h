// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TYPECD_USB_DEVICE_H_
#define TYPECD_USB_DEVICE_H_

#include <string>

namespace typecd {

// Speed exposed in USB device sysfs that can be mapped to USB standard.
enum class UsbSpeed {
  kOther = 0,
  k1_5,    // 1.5 Mbps (USB 1.1)
  k12,     // 12 Mbps (USB 1,1)
  k480,    // 480 Mbps (USB 2.0)
  k5000,   // 5000 Mbps (USB 3.2 Gen 1)
  k10000,  // 10000 Mbps (USB 3.2 Gen 2)
  k20000,  // 20000 Mbps (USB 3.2 Gen 2x2)
};

// Device class exposed in USB device sysfs.
enum class UsbDeviceClass {
  kOther = 0,
  kNone,  // class code 0x00 (Refer to interface class)
  kHub,   // class code 0x09
};

// Version exposed in USB device sysfs, derived from bcdUSB.
enum class UsbVersion {
  kOther = 0,
  k1_0,  // 1.00
  k1_1,  // 1.10
  k2_0,  // 2.00
  k2_1,  // 2.10
  k3_0,  // 3.00
  k3_1,  // 3.10
  k3_2,  // 3.20
};

// This class is used to represent a USB device. It maintains Type C port that
// the USB device is connected to.
class UsbDevice {
 public:
  UsbDevice(int busnum, int devnum, std::string hub);

  void SetTypecPortNum(int typec_port_num) { typec_port_num_ = typec_port_num; }
  void SetSpeed(UsbSpeed speed) { speed_ = speed; }
  void SetDeviceClass(UsbDeviceClass device_class) {
    device_class_ = device_class;
  }
  void SetVersion(UsbVersion version) { version_ = version; }

  int GetBusnum() { return busnum_; }
  int GetDevnum() { return devnum_; }
  int GetTypecPortNum() { return typec_port_num_; }
  UsbSpeed GetSpeed() { return speed_; }
  UsbDeviceClass GetDeviceClass() { return device_class_; }
  UsbVersion GetVersion() { return version_; }

 private:
  int busnum_;
  int devnum_;
  int typec_port_num_;
  // Root hub number and hub port number in accordance with the USB device sysfs
  // directory name. (e.g. 2-1 if sysfs path is /sys/bus/usb/devices/2-1)
  std::string hub_;
  UsbSpeed speed_;
  // Identifies type of device.
  // https://www.usb.org/defined-class-codes
  UsbDeviceClass device_class_;
  UsbVersion version_;
};

}  // namespace typecd

#endif  // TYPECD_USB_DEVICE_H_
