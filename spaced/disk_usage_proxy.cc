// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "spaced/disk_usage_proxy.h"

#include <memory>

#include <base/logging.h>

namespace spaced {

DiskUsageProxy::DiskUsageProxy() {
  dbus::Bus::Options options;
  options.bus_type = dbus::Bus::SYSTEM;
  scoped_refptr<dbus::Bus> bus = new dbus::Bus(options);
  if (!bus->Connect()) {
    LOG(ERROR) << "D-Bus system bus is not ready";
    return;
  }

  spaced_proxy_ = std::make_unique<org::chromium::SpacedProxy>(bus);
}

int64_t DiskUsageProxy::GetFreeDiskSpace(const base::FilePath& path) {
  int64_t free_disk_space;
  brillo::ErrorPtr error;
  // Return false if call fails.
  if (!spaced_proxy_->GetFreeDiskSpace(path.value(), &free_disk_space,
                                       &error)) {
    LOG(ERROR) << "Failed to call GetFreeDiskSpace, error: "
               << error->GetMessage();
    return -1;
  }

  return free_disk_space;
}

int64_t DiskUsageProxy::GetTotalDiskSpace(const base::FilePath& path) {
  int64_t total_disk_space;
  brillo::ErrorPtr error;
  // Return false if call fails.
  if (!spaced_proxy_->GetTotalDiskSpace(path.value(), &total_disk_space,
                                        &error)) {
    LOG(ERROR) << "Failed to call GetTotalDiskSpace, error: "
               << error->GetMessage();
    return -1;
  }

  return total_disk_space;
}

int64_t DiskUsageProxy::GetRootDeviceSize() {
  int64_t root_device_size;

  brillo::ErrorPtr error;
  // Return false if call fails.
  if (!spaced_proxy_->GetRootDeviceSize(&root_device_size, &error)) {
    LOG(ERROR) << "Failed to call GetRootDeviceSize, error: "
               << error->GetMessage();
    return -1;
  }

  return root_device_size;
}

}  // namespace spaced
