// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/events/udev_events_impl.h"

#include <string>
#include <utility>

#include <base/bind.h>
#include <base/check.h>
#include <base/logging.h>
#include <brillo/udev/udev_device.h>

namespace diagnostics {

UdevEventsImpl::UdevEventsImpl(Context* context) : context_(context) {
  DCHECK(context_);
  if (!context_->udev_monitor()->EnableReceiving()) {
    LOG(ERROR) << "Failed to enable receiving for udev monitor.";
    return;
  }

  int fd = context_->udev_monitor()->GetFileDescriptor();
  if (fd == brillo::UdevMonitor::kInvalidFileDescriptor) {
    LOG(ERROR) << "Failed to get udev monitor fd.";
    return;
  }

  udev_monitor_watcher_ = base::FileDescriptorWatcher::WatchReadable(
      fd, base::BindRepeating(&UdevEventsImpl::OnUdevEvent,
                              base::Unretained(this)));

  if (!udev_monitor_watcher_) {
    LOG(ERROR) << "Failed to start watcher for udev monitor fd.";
    return;
  }
}

void UdevEventsImpl::OnUdevEvent() {
  auto device = context_->udev_monitor()->ReceiveDevice();
  if (!device) {
    LOG(ERROR) << "Udev receive device failed.";
    return;
  }

  auto action = std::string(device->GetAction());
  if (action.empty()) {
    LOG(ERROR) << "Failed to get device action.";
    return;
  }

  auto subsystem = std::string(device->GetSubsystem());
  if (subsystem.empty()) {
    LOG(ERROR) << "Failed to get device subsystem";
    return;
  }

  // Distinguished events by subsystem and action here.
}

}  // namespace diagnostics
