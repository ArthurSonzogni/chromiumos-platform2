// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "primary_io_manager/udev_scopers.h"
#include <libudev.h>

namespace primary_io_manager {

void UdevDeleter::operator()(udev* udev) const {
  udev_unref(udev);
}

void UdevEnumerateDeleter::operator()(udev_enumerate* enumerate) const {
  udev_enumerate_unref(enumerate);
}

void UdevDeviceDeleter::operator()(udev_device* device) const {
  udev_device_unref(device);
}

void UdevMonitorDeleter::operator()(udev_monitor* monitor) const {
  udev_monitor_unref(monitor);
}

}  // namespace primary_io_manager
