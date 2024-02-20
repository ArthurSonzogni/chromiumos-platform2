// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "primary_io_manager/udev.h"

#include <libudev.h>
#include <memory>
#include <utility>

#include "base/files/file_descriptor_watcher_posix.h"
#include "base/functional/bind.h"
#include "base/logging.h"

namespace primary_io_manager {

UdevImpl::UdevImpl() = default;

bool UdevImpl::Init(const DeviceCallback& device_added_callback,
                    const DeviceCallback& device_removed_callback) {
  device_added_callback_ = device_added_callback;
  device_removed_callback_ = device_removed_callback;

  udev_.reset(udev_new());
  if (!udev_) {
    LOG(ERROR) << "Failed to create udev object.";
    return false;
  }

  monitor_.reset(udev_monitor_new_from_netlink(udev_.get(), "udev"));
  if (!monitor_) {
    LOG(ERROR) << "Failed to create udev monitor.";
    return false;
  }

  if (udev_monitor_filter_add_match_subsystem_devtype(monitor_.get(), "input",
                                                      nullptr) < 0) {
    LOG(ERROR) << "Failed to add 'input' subsystem filter to monitor.";
    return false;
  }

  if (udev_monitor_enable_receiving(monitor_.get()) < 0) {
    LOG(ERROR) << "Failed to enable receiving on udev monitor.";
    return false;
  }

  watcher_ = base::FileDescriptorWatcher::WatchReadable(
      udev_monitor_get_fd(monitor_.get()),
      base::BindRepeating(&UdevImpl::OnDeviceAction,
                          weak_factory_.GetWeakPtr()));
  if (!watcher_) {
    LOG(ERROR) << "Failed to register listener on udev monitor fd.";
    return false;
  }

  return true;
}

UdevImpl::~UdevImpl() = default;

void UdevImpl::OnDeviceAction() {
  ScopedUdevDevicePtr device(udev_monitor_receive_device(monitor_.get()));
  if (!device) {
    return;
  }

  const char* action = udev_device_get_action(device.get());
  if (!action) {
    LOG(WARNING) << "Failed to get device action";
    return;
  }

  const char* path = udev_device_get_devnode(device.get());
  if (!path) {
    // LOG(WARNING) << "Failed to get device path";
    return;
  }

  if (!strcmp(action, "add")) {
    device_added_callback_.Run(std::move(device));
  } else if (!strcmp(action, "remove")) {
    device_removed_callback_.Run(std::move(device));
  }
}

UdevFactory::~UdevFactory() = default;

UdevImplFactory::UdevImplFactory() = default;

UdevImplFactory::~UdevImplFactory() = default;

std::unique_ptr<Udev> UdevImplFactory::Create(
    const Udev::DeviceCallback& device_added_callback,
    const Udev::DeviceCallback& device_removed_callback) const {
  auto udev = std::make_unique<UdevImpl>();
  if (!udev->Init(device_added_callback, device_removed_callback)) {
    return nullptr;
  }

  return udev;
}

}  // namespace primary_io_manager
