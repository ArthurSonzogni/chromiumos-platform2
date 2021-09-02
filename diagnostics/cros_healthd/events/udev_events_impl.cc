// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/events/udev_events_impl.h"

#include <string>
#include <utility>

#include <base/bind.h>
#include <base/check.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <brillo/udev/udev_device.h>

#include "diagnostics/cros_healthd/utils/file_utils.h"

namespace diagnostics {

UdevEventsImpl::UdevEventsImpl(Context* context) : context_(context) {
  DCHECK(context_);
}

void UdevEventsImpl::Initialize() {
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
  if (subsystem == "thunderbolt") {
    if (action == "add") {
      OnThunderboltAddEvent();
    } else if (action == "remove") {
      OnThunderboltRemoveEvent();
    } else if (action == "change") {
      auto path = base::FilePath(device->GetSysPath());
      std::string authorized;
      if (ReadAndTrimString(path.Append("authorized"), &authorized)) {
        unsigned auth;
        base::StringToUint(authorized, &auth);
        auth ? OnThunderboltAuthorizedEvent()
             : OnThunderboltUnAuthorizedEvent();
      }
    }
  }
}

void UdevEventsImpl::AddThunderboltObserver(
    mojo::PendingRemote<
        chromeos::cros_healthd::mojom::CrosHealthdThunderboltObserver>
        observer) {
  thunderbolt_observers_.Add(std::move(observer));
}

void UdevEventsImpl::OnThunderboltAddEvent() {
  for (auto& observer : thunderbolt_observers_)
    observer->OnAdd();
}

void UdevEventsImpl::OnThunderboltRemoveEvent() {
  for (auto& observer : thunderbolt_observers_)
    observer->OnRemove();
}

void UdevEventsImpl::OnThunderboltAuthorizedEvent() {
  for (auto& observer : thunderbolt_observers_)
    observer->OnAuthorized();
}

void UdevEventsImpl::OnThunderboltUnAuthorizedEvent() {
  for (auto& observer : thunderbolt_observers_)
    observer->OnUnAuthorized();
}

}  // namespace diagnostics
