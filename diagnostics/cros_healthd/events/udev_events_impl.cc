// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/events/udev_events_impl.h"

#include <libusb.h>
#include <set>
#include <string>
#include <utility>

#include <base/bind.h>
#include <base/check.h>
#include <base/files/file_enumerator.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <brillo/udev/udev_device.h>

#include "diagnostics/cros_healthd/utils/file_utils.h"
#include "diagnostics/cros_healthd/utils/usb_utils.h"
#include "diagnostics/cros_healthd/utils/usb_utils_constants.h"
#include "diagnostics/mojom/public/cros_healthd_events.mojom.h"

namespace diagnostics {
namespace {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

std::string GetString(const char* str) {
  if (str) {
    return std::string(str);
  }

  return "";
}

void FillUsbCategory(const std::unique_ptr<brillo::UdevDevice>& device,
                     const base::FilePath& root_dir,
                     mojo_ipc::UsbEventInfo* info) {
  auto sys_path = GetString(device->GetSysPath());
  uint32_t class_code = 0;
  std::set<std::string> categories;

  ReadInteger(base::FilePath(sys_path), kFileUsbDevClass,
              &base::HexStringToUInt, &class_code);
  if (class_code != libusb_class_code::LIBUSB_CLASS_PER_INTERFACE) {
    categories.insert(LookUpUsbDeviceClass(class_code));
  } else {  // The category is determined by interfaces.
    base::FileEnumerator file_enum(base::FilePath(sys_path), false,
                                   base::FileEnumerator::FileType::DIRECTORIES);
    for (auto path = file_enum.Next(); !path.empty(); path = file_enum.Next()) {
      std::string content;
      ReadAndTrimString(path.Append(kFileUsbIFClass), &content);
      if (!base::HexStringToUInt(content, &class_code))
        continue;
      categories.insert(LookUpUsbDeviceClass(class_code));
    }
  }

  categories.erase("Unknown");
  for (const auto& category : categories) {
    info->categories.push_back(category);
  }
}

void FillUsbEventInfo(const std::unique_ptr<brillo::UdevDevice>& device,
                      const base::FilePath& root_dir,
                      mojo_ipc::UsbEventInfo* info) {
  info->vendor = GetUsbVendorName(device);
  info->name = GetUsbProductName(device);
  std::tie(info->vid, info->pid) = GetUsbVidPid(device);
  FillUsbCategory(device, root_dir, info);
}

}  // namespace

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

  auto action = GetString(device->GetAction());
  auto subsystem = GetString(device->GetSubsystem());
  auto device_type = GetString(device->GetDeviceType());

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
  } else if (subsystem == "usb" && device_type == "usb_device") {
    if (action == "add") {
      OnUsbAdd(device);
    } else if (action == "remove") {
      OnUsbRemove(device);
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

void UdevEventsImpl::AddUsbObserver(
    mojo::PendingRemote<chromeos::cros_healthd::mojom::CrosHealthdUsbObserver>
        observer) {
  usb_observers_.Add(std::move(observer));
}

void UdevEventsImpl::OnUsbAdd(
    const std::unique_ptr<brillo::UdevDevice>& device) {
  mojo_ipc::UsbEventInfo info;
  FillUsbEventInfo(device, context_->root_dir(), &info);
  for (auto& observer : usb_observers_)
    observer->OnAdd(info.Clone());
}

void UdevEventsImpl::OnUsbRemove(
    const std::unique_ptr<brillo::UdevDevice>& device) {
  mojo_ipc::UsbEventInfo info;
  FillUsbEventInfo(device, context_->root_dir(), &info);
  for (auto& observer : usb_observers_)
    observer->OnRemove(info.Clone());
}

}  // namespace diagnostics
