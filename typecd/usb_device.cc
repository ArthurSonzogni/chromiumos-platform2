// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "typecd/usb_device.h"

#include <base/logging.h>

namespace typecd {

UsbDevice::UsbDevice(int busnum, int devnum, std::string hub)
    : busnum_(busnum),
      devnum_(devnum),
      typec_port_num_(-1),
      hub_(hub),
      speed_(-1),
      device_class_(-1),
      interface_class_(-1) {
  LOG(INFO) << "USB device " << hub_ << " enumerated.";
}

}  // namespace typecd
