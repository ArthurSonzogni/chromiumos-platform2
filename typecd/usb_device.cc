// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "typecd/usb_device.h"

namespace typecd {

UsbDevice::UsbDevice(int busnum, int devnum)
    : busnum_(busnum), devnum_(devnum), typec_port_num_(-1) {}

}  // namespace typecd
