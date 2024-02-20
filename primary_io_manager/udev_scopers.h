// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PRIMARY_IO_MANAGER_UDEV_SCOPERS_H_
#define PRIMARY_IO_MANAGER_UDEV_SCOPERS_H_

#include <libudev.h>

#include <memory>

namespace primary_io_manager {

struct UdevDeleter {
  void operator()(udev* udev) const;
};

struct UdevEnumerateDeleter {
  void operator()(udev_enumerate* enumerate) const;
};

struct UdevDeviceDeleter {
  void operator()(udev_device* device) const;
};

struct UdevMonitorDeleter {
  void operator()(udev_monitor* monitor) const;
};

typedef std::unique_ptr<udev, UdevDeleter> ScopedUdevPtr;
typedef std::unique_ptr<udev_enumerate, UdevEnumerateDeleter>
    ScopedUdevEnumeratePtr;
typedef std::unique_ptr<udev_device, UdevDeviceDeleter> ScopedUdevDevicePtr;
typedef std::unique_ptr<udev_monitor, UdevMonitorDeleter> ScopedUdevMonitorPtr;

}  // namespace primary_io_manager

#endif  // PRIMARY_IO_MANAGER_UDEV_SCOPERS_H_
